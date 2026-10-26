// The info of an event flag and of a manual reset simple event while three waiters leave their
// queue one step at a time. Each step prints the waiting thread count and the pattern, and for the
// simple event the low word of the user data, right after the call that wakes a waiter or lets a
// deadline pass. Every thread is pinned to core 0, the main thread at 0x60 and the waiters at 0xB0,
// so a woken waiter stays ready until the main thread blocks. The reads don't depend on what a
// woken waiter does after its wake, which only the joins show. The cases:
// - an event flag with the multiple waiter attribute, FIFO. W0 waits for bit 0x1 with OR and
//   CLEAR_PAT, W1 for 0x1 with OR, W2 for 0x6 with AND. The main thread sets 0x1, then 0x6. Then it
//   queues W3 for bit 0x8 with OR and a short timeout, and spins past W3's deadline while W3 can't
//   run. Last, a set of 0x1 releases W1.
// - a manual reset simple event with W0 waiting for A, W1 for A or B, and W2 for B. The main thread
//   sets A with user data 0x11, then B with user data 0x22.
// After that the main thread joins the waiters and prints each result, and the bits and user data
// each one got when it got them.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
// The timeout of the waiter whose deadline passes during the spin
#define SHORT_US 300000
// The spin runs well past SHORT_US, and no call in it blocks
#define SPIN_US 1600000
// The timeout of every wait that a set is expected to end
#define LONG_US (3 * TEST_WAIT_US)

enum { W0, W1, W2, W3, WAITERS };

// What a waiter waits for: the pattern, the mode of an event flag wait, and the timeout
typedef struct Wait {
    SceUInt32 pattern, mode;
    SceUInt timeout;
} Wait;

static SceUID flag, event;
static Wait waits[WAITERS];
static int results[WAITERS];
static SceUInt32 bits[WAITERS];
static SceUInt64 user_data[WAITERS];

// Waits on the event flag for waits[i], where i is the argument
static int flag_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = waits[i].timeout;
    results[i] = sceKernelWaitEventFlag(flag, waits[i].pattern, waits[i].mode, &bits[i], &timeout);
    return 0;
}

// Waits on the simple event for waits[i], where i is the argument
static int event_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = waits[i].timeout;
    results[i] = sceKernelWaitEvent(event, waits[i].pattern, &bits[i], &user_data[i], &timeout);
    return 0;
}

// Starts waiter i on entry and returns once it waits with the wait type on the object
static SceUID start_waiter(int i, SceKernelThreadEntry entry, SceUInt32 type, SceUID object) {
    results[i] = NOT_DONE;
    bits[i] = NOT_DONE;
    user_data[i] = NOT_DONE;
    const SceUID thread = sceKernelCreateThread(
        "waiter", entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, sizeof(i), &i);
    TEST_WAIT_FOR(test_in_wait(thread, type, object));
    return thread;
}

// Spins on the process clock, so the main thread keeps core 0 and never blocks
static void spin(void) {
    const SceUInt64 end = sceKernelGetProcessTimeWide() + SPIN_US;
    while (sceKernelGetProcessTimeWide() < end)
        ;
}

// Prints the waiting thread count and the pattern of the flag
static void print_flag(const char *when) {
    SceKernelEventFlagInfo info = { .size = sizeof(info) };
    sceKernelGetEventFlagInfo(flag, &info);
    PRINTF(info.numWaitThreads, "event flag: the waiting threads %s", when);
    PRINTF(info.currentPattern, "event flag: the pattern %s", when);
}

// Prints the waiting thread count, the pattern bits A and B, and the low word of the user data
// when with_user_data is set, since the user data left over from create differs between runs
static void print_event(const char *when, int with_user_data) {
    EventInfo info = { .size = sizeof(info) };
    sceKernelGetEventInfo(event, &info);
    PRINTF(info.numWaitThreads, "simple event: the waiting threads %s", when);
    PRINTF(info.eventPattern & (EVENT_BIT_A | EVENT_BIT_B), "simple event: the pattern %s", when);
    if (with_user_data)
        PRINTF((SceUInt32)info.userData, "simple event: the low word of the user data %s", when);
}

// Joins the waiters, then prints each result and the bits it got
static void finish_flag(const SceUID *threads, int count) {
    for (int i = 0; i < count; i++)
        test_join_thread(threads[i]);
    for (int i = 0; i < count; i++) {
        PRINTF(results[i], "event flag: W%d's result", i);
        if (results[i] == 0)
            PRINTF(bits[i], "event flag: the bits W%d got", i);
    }
}

// Joins the waiters, then prints each result, and the bits and user data each one got
static void finish_event(const SceUID *threads, int count) {
    for (int i = 0; i < count; i++)
        test_join_thread(threads[i]);
    for (int i = 0; i < count; i++) {
        PRINTF(results[i], "simple event: W%d's result", i);
        if (results[i] == 0) {
            PRINTF(bits[i] & (EVENT_BIT_A | EVENT_BIT_B), "simple event: the bits W%d got", i);
            PRINTF((SceUInt32)user_data[i], "simple event: the low word of W%d's user data", i);
        }
    }
}

static void flag_case(void) {
    SceUID threads[WAITERS];
    flag = sceKernelCreateEventFlag("flag", SCE_EVENT_WAITMULTIPLE, 0, NULL);
    waits[W0] = (Wait){ .pattern = 0x1, .mode = SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT,
        .timeout = LONG_US };
    waits[W1] = (Wait){ .pattern = 0x1, .mode = SCE_EVENT_WAITOR, .timeout = LONG_US };
    waits[W2] = (Wait){ .pattern = 0x6, .mode = SCE_EVENT_WAITAND, .timeout = LONG_US };
    waits[W3] = (Wait){ .pattern = 0x8, .mode = SCE_EVENT_WAITOR, .timeout = SHORT_US };

    for (int i = W0; i <= W2; i++)
        threads[i] = start_waiter(i, flag_waiter, WAIT_TYPE_EVENT_FLAG, flag);
    print_flag("with three waiters");
    sceKernelSetEventFlag(flag, 0x1);
    print_flag("after a set of 0x1");
    sceKernelSetEventFlag(flag, 0x6);
    print_flag("after a set of 0x6");

    threads[W3] = start_waiter(W3, flag_waiter, WAIT_TYPE_EVENT_FLAG, flag);
    print_flag("with the short waiter queued");
    spin();
    print_flag("after the short waiter's deadline");
    sceKernelSetEventFlag(flag, 0x1);
    print_flag("after a set of 0x1 for W1");

    finish_flag(threads, WAITERS);
    sceKernelDeleteEventFlag(flag);
}

static void event_case(void) {
    SceUID threads[3];
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    waits[W0] = (Wait){ .pattern = EVENT_BIT_A, .timeout = LONG_US };
    waits[W1] = (Wait){ .pattern = EVENT_BIT_A | EVENT_BIT_B, .timeout = LONG_US };
    waits[W2] = (Wait){ .pattern = EVENT_BIT_B, .timeout = LONG_US };

    for (int i = W0; i <= W2; i++)
        threads[i] = start_waiter(i, event_waiter, WAIT_TYPE_EVENT, event);
    print_event("with three waiters", 0);
    sceKernelSetEvent(event, EVENT_BIT_A, 0x11);
    print_event("after a set of A", 1);
    sceKernelSetEvent(event, EVENT_BIT_B, 0x22);
    print_event("after a set of B", 1);

    finish_event(threads, 3);
    sceKernelDeleteSimpleEvent(event);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    flag_case();
    event_case();

    test_exit();
    return 0;
}
