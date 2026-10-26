// Whether the exclusive monitor that an ldrex sets is still there for the strex that follows it,
// when something runs in between. Each case runs in a thread L on core 0: L does ldrex on a word,
// does what the case says, then strex stores a new value to the word. The main thread on core 1
// prints the strex result, 0 when the value was stored and 1 when the strex failed, and the word
// after. The cases are:
// - nothing between the two
// - a clrex between the two, as a control for a monitor that is cleared
// - a PollSema call between the two, a call that doesn't wait
// - a delay of 1 ms between the two, in which another thread T on core 0, of the lowest priority,
//   does its own ldrex and strex on the same word and stores its own value. L waits for T to
//   end before its strex, and prints T's strex result.
// - a callback run by sceKernelCheckCallback between the two, which does an ldrex and strex on
//   another word and prints its strex result and how often it ran
// - a preemption between the two. L signals that it spins, then spins on a flag with only loads
//   between the two. The main thread on core 1 wakes H, a higher priority thread on core 0 that
//   was waiting on a semaphore, and H sets the flag and ends. Prints whether H ran during the spin.
// Each case runs twice, once with the ldrex and the strex in Thumb helpers and once with them in
// ARM helpers. Every case starts fresh threads and objects, and every wait has a finite timeout.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define OLD_WORD 0x11111111U
#define NEW_WORD 0x22222222U
#define OTHER_WORD 0x33333333U
#define CALLBACK_OLD 0x44444444U
#define CALLBACK_NEW 0x55555555U
#define LOW_PRIORITY 0xA0
#define HIGH_PRIORITY 0x80
// The most loops the spin runs, about a second on a Vita. H is woken long before that.
#define SPINS 150000000

typedef enum Between { NOTHING, CLREX, POLL, DELAY, CALLBACK, PREEMPT } Between;

static unsigned int word, callback_word;
static volatile unsigned int strex_result;
static volatile int arm, between;
static volatile int spinning, other_ran, other_done, other_result;
static volatile int callback_runs, callback_result;
static volatile int poll_result;
static SceUID sema, high_sema, callback;

// ldrex and strex in Thumb state, each alone in its helper, so no other code runs between the
// two instructions of a pair
__attribute__((naked)) static unsigned int ldrex_thumb(unsigned int *addr) {
    (void)addr;
    __asm__ volatile("ldrex r0, [r0]\n\t"
                     "bx lr\n\t");
}

__attribute__((naked)) static unsigned int strex_thumb(unsigned int *addr, unsigned int value) {
    (void)addr;
    (void)value;
    __asm__ volatile("strex r2, r1, [r0]\n\t"
                     "mov r0, r2\n\t"
                     "bx lr\n\t");
}

// The same in ARM state
__attribute__((naked, target("arm"))) static unsigned int ldrex_arm(unsigned int *addr) {
    (void)addr;
    __asm__ volatile("ldrex r0, [r0]\n\t"
                     "bx lr\n\t");
}

__attribute__((naked, target("arm"))) static unsigned int strex_arm(unsigned int *addr, unsigned int value) {
    (void)addr;
    (void)value;
    __asm__ volatile("strex r2, r1, [r0]\n\t"
                     "mov r0, r2\n\t"
                     "bx lr\n\t");
}

static unsigned int load_exclusive(unsigned int *addr) {
    return arm ? ldrex_arm(addr) : ldrex_thumb(addr);
}

static unsigned int store_exclusive(unsigned int *addr, unsigned int value) {
    return arm ? strex_arm(addr, value) : strex_thumb(addr, value);
}

// Stores its own value to the word with its own ldrex and strex, while L is in its delay
static int other_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    load_exclusive(&word);
    other_result = store_exclusive(&word, OTHER_WORD);
    other_done = 1;
    return 0;
}

// Does an ldrex and strex on a word of its own, so a strex of L's outcome can't depend on it
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    load_exclusive(&callback_word);
    callback_result = store_exclusive(&callback_word, CALLBACK_NEW);
    return 0;
}

// Waits on the semaphore until L spins, then sets the flag L spins on and ends
static int high_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    other_ran = sceKernelWaitSema(high_sema, 1, &timeout) == 0;
    return 0;
}

// The low thread: ldrex, the case, then strex
static int low_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUID other = 0;
    if (between == CALLBACK) {
        callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
        sceKernelNotifyCallback(callback, 1);
    }
    load_exclusive(&word);
    switch (between) {
    case NOTHING:
        break;
    case CLREX:
        __asm__ volatile("clrex");
        break;
    case POLL:
        poll_result = sceKernelPollSema(sema, 1);
        break;
    case DELAY:
        other = sceKernelCreateThread("other", other_entry, SCE_KERNEL_LOWEST_PRIORITY_USER, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(other, 0, NULL);
        sceKernelDelayThread(1000);
        TEST_WAIT_FOR(other_done);
        break;
    case CALLBACK:
        sceKernelCheckCallback();
        break;
    case PREEMPT: {
        spinning = 1;
        unsigned int count = SPINS;
        unsigned int seen;
        __asm__ volatile(
            "1:\n\t"
            "ldr %[seen], [%[flag]]\n\t"
            "cmp %[seen], #0\n\t"
            "bne 2f\n\t"
            "subs %[count], %[count], #1\n\t"
            "bne 1b\n\t"
            "2:\n\t"
            : [count] "+r"(count), [seen] "=&r"(seen)
            : [flag] "r"(&other_ran)
            : "cc", "memory");
        break;
    }
    }
    strex_result = store_exclusive(&word, NEW_WORD);
    if (other)
        test_join_thread(other);
    if (between == CALLBACK)
        sceKernelDeleteCallback(callback);
    return 0;
}

static void run_case(const char *pair, Between kind, int use_arm, const char *what) {
    between = kind;
    arm = use_arm;
    word = OLD_WORD;
    callback_word = CALLBACK_OLD;
    strex_result = 0xDEAD;
    spinning = other_ran = other_done = other_result = 0;
    callback_runs = callback_result = 0;
    poll_result = 0xDEAD;
    sema = sceKernelCreateSema("poll", 0, 1, 1, NULL);
    high_sema = sceKernelCreateSema("high", 0, 0, 1, NULL);

    SceUID high = 0;
    if (kind == PREEMPT) {
        high = sceKernelCreateThread("high", high_entry, HIGH_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(high, 0, NULL);
        test_wait_until_waiting(high);
    }
    const SceUID low = sceKernelCreateThread("low", low_entry, LOW_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(low, 0, NULL);
    if (kind == PREEMPT) {
        TEST_WAIT_FOR(spinning);
        sceKernelSignalSema(high_sema, 1);
        test_join_thread(high);
    }
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(low, NULL, &timeout);
    sceKernelDeleteThread(low);

    sceKernelDeleteSema(sema);
    sceKernelDeleteSema(high_sema);

    switch (kind) {
    case POLL:
        PRINTF(poll_result, "%s, %s: the PollSema result", pair, what);
        break;
    case DELAY:
        PRINTF(other_result, "%s, %s: the other thread's strex result", pair, what);
        break;
    case CALLBACK:
        PRINTF(callback_runs, "%s, %s: the callback runs", pair, what);
        PRINTF(callback_result, "%s, %s: the callback's strex result", pair, what);
        break;
    case PREEMPT:
        PRINTF(other_ran, "%s, %s: H ran during L's spin", pair, what);
        break;
    default:
        break;
    }
    PRINTF(strex_result, "%s, %s: the strex result", pair, what);
    PRINTF(word, "%s, %s: the word after", pair, what);
}

static void run_all(const char *pair, int use_arm) {
    run_case(pair, NOTHING, use_arm, "nothing between");
    run_case(pair, CLREX, use_arm, "a clrex between");
    run_case(pair, POLL, use_arm, "a PollSema call between");
    run_case(pair, DELAY, use_arm, "a 1 ms delay between, with another thread on the core");
    run_case(pair, CALLBACK, use_arm, "a callback run by CheckCallback between");
    run_case(pair, PREEMPT, use_arm, "a preemption between, by a higher priority thread");
}

int main(void) {
    // The main thread moves to core 1 when it is next scheduled. The case threads are on core 0.
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    sceKernelDelayThread(1000);

    run_all("Thumb", 0);
    run_all("ARM", 1);

    test_exit();
    return 0;
}
