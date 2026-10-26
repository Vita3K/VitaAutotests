// When sceKernelStartThread copies the argument of a new thread onto the thread's stack, and the
// largest argument it takes for a stack. The sender starts the thread with a 16 byte buffer and
// then changes the buffer, so the entry shows whether it got the bytes that were sent or the bytes
// the buffer held when the thread first ran. Cases:
// - below main: the new thread is below the main thread on the same core, so it can't run until
//   the main thread waits. The main thread changes the buffer to 0x55 after the start returns.
// - above main: the control, a new thread above the main thread on the same core, which runs before
//   the start returns. The main thread changes the buffer only after the entry recorded its bytes.
// - entry writes: the entry writes to its copy, and the sender's buffer is checked after the end.
// - restart: a restart with new bytes gets a fresh copy of them.
// - sizes: argument sizes near the rounded stack size and one past it, for stacks of 0x1000 bytes
//   and of 0x1001 bytes (rounded up to 0x2000). An argument that fills the whole stack is left out,
//   since such a start crashed the app on a PS Vita TV. The entry is naked and uses no stack. These
//   cases go last, since a wrong size check may overrun a stack.
// The sizes entry records its r0 and sp, and the test prints how far sp is below the top of the
// stack. Each case starts new threads, and the cases wait for each thread with a timeout.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
// Below the main thread, so it can't run until the main thread waits
#define LOW_PRIORITY 0xB8
// Above the main thread, so it runs before the start returns
#define HIGH_PRIORITY 0x80

#define ARG_SIZE 16
#define OVERWRITE 0x55
#define WRITTEN 0xCC

// The bytes sent as the argument, each different from the next
static const unsigned char sent[ARG_SIZE] = {
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
};

// The bytes the copy entry got, and whether it ran
static volatile unsigned char got[ARG_SIZE];
static volatile int got_ran;
// Whether the copy entry writes to its copy, and how many bytes of the copy read back as written
static volatile int write_copy, wrote_back;

// Records the bytes of the argument it gets, then writes to them if write_copy is set
static int copy_entry(SceSize args, void *argp) {
    (void)args;
    volatile unsigned char *copy = argp;
    for (int i = 0; i < ARG_SIZE; i++)
        got[i] = copy[i];
    if (write_copy) {
        int read_back = 0;
        for (int i = 0; i < ARG_SIZE; i++)
            copy[i] = WRITTEN;
        for (int i = 0; i < ARG_SIZE; i++)
            read_back += copy[i] == WRITTEN;
        wrote_back = read_back;
    }
    got_ran = 1;
    return 0;
}

// Counts the bytes of buf that equal the bytes of other at the same place
static int count_same(const volatile unsigned char *buf, const unsigned char *other, int size) {
    int n = 0;
    for (int i = 0; i < size; i++)
        n += buf[i] == other[i];
    return n;
}

// Counts the bytes of buf that equal value
static int count_value(const volatile unsigned char *buf, unsigned char value, int size) {
    int n = 0;
    for (int i = 0; i < size; i++)
        n += buf[i] == value;
    return n;
}

// Returns whether the calling thread is on core 0
static int on_core_0(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    return info.currentCpuId == 0;
}

static void reset_got(void) {
    for (int i = 0; i < ARG_SIZE; i++)
        got[i] = 0;
    got_ran = 0;
}

// Creates a copy_entry thread on core 0, so it shares the core with the main thread
static SceUID create_copy_thread(int priority) {
    return sceKernelCreateThread("copy", copy_entry, priority, 0x4000, 0,
                                 SCE_KERNEL_CPU_MASK_USER_0, NULL);
}

// A thread below the main thread. The main thread changes its buffer after the start returns, so
// the entry gets the changed bytes only if the copy is made when the thread first runs.
static void case_below_main(void) {
    unsigned char buf[ARG_SIZE];
    for (int i = 0; i < ARG_SIZE; i++)
        buf[i] = sent[i];
    reset_got();
    const SceUID thread = create_copy_thread(LOW_PRIORITY);
    PRINTF(sceKernelStartThread(thread, sizeof(buf), buf), "below main: start");
    for (int i = 0; i < ARG_SIZE; i++)
        buf[i] = OVERWRITE;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);

    PRINTF(got_ran, "below main: the entry ran");
    PRINTF(count_same(got, sent, ARG_SIZE), "below main: bytes the entry got that were sent");
    PRINTF(count_value(got, OVERWRITE, ARG_SIZE), "below main: bytes the entry got that are 0x55");
}

// A thread above the main thread, which runs before the start returns. The main thread waits for
// the entry to record its bytes before it changes the buffer, so this case is the control for
// below_main.
static void case_above_main(void) {
    unsigned char buf[ARG_SIZE];
    for (int i = 0; i < ARG_SIZE; i++)
        buf[i] = sent[i];
    reset_got();
    const SceUID thread = create_copy_thread(HIGH_PRIORITY);
    PRINTF(sceKernelStartThread(thread, sizeof(buf), buf), "above main: start");
    TEST_WAIT_FOR(got_ran);
    for (int i = 0; i < ARG_SIZE; i++)
        buf[i] = OVERWRITE;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);

    PRINTF(got_ran, "above main: the entry ran");
    PRINTF(count_same(got, sent, ARG_SIZE), "above main: bytes the entry got that were sent");
    PRINTF(count_value(got, OVERWRITE, ARG_SIZE), "above main: bytes the entry got that are 0x55");
}

// The entry writes to its copy. The sender's buffer must keep the bytes that were sent.
static void case_entry_writes(void) {
    unsigned char buf[ARG_SIZE];
    for (int i = 0; i < ARG_SIZE; i++)
        buf[i] = sent[i];
    reset_got();
    write_copy = 1;
    wrote_back = 0;
    const SceUID thread = create_copy_thread(LOW_PRIORITY);
    PRINTF(sceKernelStartThread(thread, sizeof(buf), buf), "entry writes: start");
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);
    write_copy = 0;

    PRINTF(got_ran, "entry writes: the entry ran");
    PRINTF(count_same(got, sent, ARG_SIZE), "entry writes: bytes the entry got that were sent");
    PRINTF(wrote_back, "entry writes: bytes of its copy that read back as written");
    PRINTF(ARG_SIZE - count_same(buf, sent, ARG_SIZE),
           "entry writes: bytes of the sender's buffer that changed");
}

// A thread started with one argument and restarted with another, which the entry must get fresh.
static void case_restart(void) {
    unsigned char first[ARG_SIZE], second[ARG_SIZE];
    for (int i = 0; i < ARG_SIZE; i++) {
        first[i] = sent[i];
        second[i] = 0x30 + i;
    }
    reset_got();
    const SceUID thread = create_copy_thread(LOW_PRIORITY);
    PRINTF(sceKernelStartThread(thread, sizeof(first), first), "restart: first start");
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    PRINTF(count_same(got, first, ARG_SIZE),
           "restart: first start, bytes the entry got that were sent");

    reset_got();
    PRINTF(sceKernelStartThread(thread, sizeof(second), second), "restart: start with new bytes");
    timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);

    PRINTF(got_ran, "restart: the entry ran");
    PRINTF(count_same(got, second, ARG_SIZE), "restart: bytes the entry got that are the new ones");
    PRINTF(count_same(got, first, ARG_SIZE),
           "restart: bytes the entry got that are the first ones");
}

// What the sizes entry saw: its r0, its sp, and whether it ran
static volatile unsigned int size_seen[3];

// Stores r0 and sp, and that it ran, without using the stack, then returns 0
__attribute__((naked)) static int size_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    __asm__ volatile(
        "movw r2, :lower16:size_seen\n\t"
        "movt r2, :upper16:size_seen\n\t"
        "str r0, [r2]\n\t"
        "mov r1, sp\n\t"
        "str r1, [r2, #4]\n\t"
        "movs r1, #1\n\t"
        "str r1, [r2, #8]\n\t"
        "movs r0, #0\n\t"
        "bx lr\n\t");
}

// The argument the size cases send, a zeroed buffer big enough for the largest one
static unsigned char big_arg[0x2000];

// Starts a size_entry thread with a stack of stack_size and an argument of arg_size, then prints
// the start result and, if it started, the entry's r0 and how far its sp is below the stack top
static void case_size(const char *name, SceSize stack_size, SceSize arg_size) {
    size_seen[0] = size_seen[1] = size_seen[2] = 0;
    const SceUID thread = sceKernelCreateThread("size", size_entry,
                                                SCE_KERNEL_DEFAULT_PRIORITY_USER, stack_size, 0,
                                                0, NULL);
    const int start = sceKernelStartThread(thread, arg_size, big_arg);
    PRINTF(start, "%s: start", name);
    if (start == 0) {
        SceKernelThreadInfo info = { .size = sizeof(info) };
        sceKernelGetThreadInfo(thread, &info);
        PRINTF(info.stackSize, "%s: stack size", name);
        const SceUIntPtr top = (SceUIntPtr)info.stack + info.stackSize;
        SceUInt timeout = TEST_WAIT_US;
        sceKernelWaitThreadEnd(thread, NULL, &timeout);
        PRINTF(size_seen[2], "%s: the entry ran", name);
        PRINTF(size_seen[0], "%s: the entry's r0", name);
        PRINTF(top - size_seen[1], "%s: sp below the top by", name);
    }
    sceKernelDeleteThread(thread);
}

int main(void) {
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    // The move to core 0 happens when the main thread is next scheduled
    TEST_WAIT_FOR(on_core_0());

    case_below_main();
    case_above_main();
    case_entry_writes();
    case_restart();

    case_size("stack 0x1000, argument 0xFF0", 0x1000, 0xFF0);
    case_size("stack 0x1000, argument 0x1001", 0x1000, 0x1001);
    case_size("stack 0x1001, argument 0x1800", 0x1001, 0x1800);

    test_exit();
    return 0;
}
