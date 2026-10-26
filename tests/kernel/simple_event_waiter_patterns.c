// How waiters for different bits of one simple event share a set or pulse, in FIFO and priority
// order, with manual and auto reset events. Every thread is pinned to core 0. Main, at 0x60, runs
// above the waiters, so a thread that a call wakes can't run before main reads what the call left.
// Only waiter 0 of the last case is raised to main's priority, and then it ties with main. The
// cases:
// - manual reset, FIFO: waiter 0 for A, waiter 1 for A or B, waiter 2 for B. A set of A, then a set
//   of B. Then waiter 3 for C, and a pulse of A, B and C
// - manual reset: waiter 0 for A or C, waiter 1 for B, and a pulse of A and B
// - auto reset, FIFO: a set of A and B, with waiter 0 for A or B at 0x90, waiter 1 for B at 0x70
//   and waiter 2 for A at 0x80
// - the same with the priority queue attribute
// - the same with the priority queue attribute, and waiter 0 raised to 0x60 once all three wait
// Each case prints the result of each call and the bits left after it, the number of waiters the
// final cancel counted, and each waiter's result, with the bits it got when the result is 0.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define RAISED_PRIORITY 0x60

#define WAITERS 4
#define USER_BITS (EVENT_BIT_A | EVENT_BIT_B | EVENT_BIT_C)

static SceUID event;
static SceUID threads[WAITERS];
static volatile SceUInt32 wanted[WAITERS];
static volatile int results[WAITERS];
static volatile SceUInt32 patterns[WAITERS];

static int waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt32 pattern = 0xDEAD;
    SceUInt64 user_data;
    SceUInt timeout = 3 * TEST_WAIT_US;
    results[i] = sceKernelWaitEvent(event, wanted[i], &pattern, &user_data, &timeout);
    patterns[i] = pattern;
    return 0;
}

// Starts waiter i for bits at priority, and returns once it is queued
static void start_waiter(int i, SceUInt32 bits, int priority) {
    wanted[i] = bits;
    results[i] = 0xDEAD;
    patterns[i] = 0xDEAD;
    threads[i] = sceKernelCreateThread(
        "waiter", waiter, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(threads[i], sizeof(i), &i);
    test_wait_until_waiting(threads[i]);
}

static SceUInt32 event_pattern(void) {
    SceUInt32 pattern = 0xDEAD;
    sceKernelGetEventPattern(event, &pattern);
    return pattern & USER_BITS;
}

// Cancels the waits left and joins the waiters, and returns how many the cancel counted
static SceUInt32 end_waits(int count) {
    SceUInt32 waiting = 0xDEAD;
    sceKernelCancelEvent(event, &waiting);
    for (int i = 0; i < count; i++)
        test_join_thread(threads[i]);
    return waiting;
}

// Prints the result of each waiter, and the bits of those that got a result
static void print_waiters(const char *name, int count) {
    for (int i = 0; i < count; i++) {
        PRINTF(results[i], "%s: waiter %d's result", name, i);
        if (results[i] == 0)
            PRINTF(patterns[i] & USER_BITS, "%s: the bits waiter %d got", name, i);
    }
}

static void manual_fifo_case(void) {
    const char *name = "manual reset, FIFO";
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    start_waiter(0, EVENT_BIT_A, WAITER_PRIORITY);
    start_waiter(1, EVENT_BIT_A | EVENT_BIT_B, WAITER_PRIORITY);
    start_waiter(2, EVENT_BIT_B, WAITER_PRIORITY);
    const int set_a = sceKernelSetEvent(event, EVENT_BIT_A, 0);
    const SceUInt32 left_a = event_pattern();
    const int set_b = sceKernelSetEvent(event, EVENT_BIT_B, 0);
    const SceUInt32 left_b = event_pattern();
    start_waiter(3, EVENT_BIT_C, WAITER_PRIORITY);
    const int pulse = sceKernelPulseEvent(event, USER_BITS, 0);
    const SceUInt32 left_pulse = event_pattern();
    const SceUInt32 waiting = end_waits(WAITERS);

    PRINTF(set_a, "%s: the set of A", name);
    PRINTF(left_a, "%s: the bits left after the set of A", name);
    PRINTF(set_b, "%s: the set of B", name);
    PRINTF(left_b, "%s: the bits left after the set of B", name);
    PRINTF(pulse, "%s: the pulse of A, B and C", name);
    PRINTF(left_pulse, "%s: the bits left after the pulse", name);
    PRINTF(waiting, "%s: the waiters the cancel counted", name);
    print_waiters(name, WAITERS);
    sceKernelDeleteSimpleEvent(event);
}

static void manual_pulse_case(void) {
    const char *name = "manual reset, pulse of A and B";
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    start_waiter(0, EVENT_BIT_A | EVENT_BIT_C, WAITER_PRIORITY);
    start_waiter(1, EVENT_BIT_B, WAITER_PRIORITY);
    const int pulse = sceKernelPulseEvent(event, EVENT_BIT_A | EVENT_BIT_B, 0);
    const SceUInt32 left = event_pattern();
    const SceUInt32 waiting = end_waits(2);

    PRINTF(pulse, "%s: the pulse", name);
    PRINTF(left, "%s: the bits left", name);
    PRINTF(waiting, "%s: the waiters the cancel counted", name);
    print_waiters(name, 2);
    sceKernelDeleteSimpleEvent(event);
}

// A set of A and B among three auto reset waiters, with waiter 0 raised first if raise is set
static void auto_case(const char *name, SceUInt32 attr, int raise) {
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET | attr, 0, NULL);
    start_waiter(0, EVENT_BIT_A | EVENT_BIT_B, 0x90);
    start_waiter(1, EVENT_BIT_B, 0x70);
    start_waiter(2, EVENT_BIT_A, 0x80);
    if (raise)
        sceKernelChangeThreadPriority(threads[0], RAISED_PRIORITY);
    const int set = sceKernelSetEvent(event, EVENT_BIT_A | EVENT_BIT_B, 0);
    const SceUInt32 left = event_pattern();
    const SceUInt32 waiting = end_waits(3);

    PRINTF(set, "%s: the set of A and B", name);
    PRINTF(left, "%s: the bits left", name);
    PRINTF(waiting, "%s: the waiters the cancel counted", name);
    print_waiters(name, 3);
    sceKernelDeleteSimpleEvent(event);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    manual_fifo_case();
    manual_pulse_case();
    auto_case("auto reset, FIFO", 0, 0);
    auto_case("auto reset, priority queue", SCE_KERNEL_ATTR_THREAD_PRIO, 0);
    auto_case("auto reset, priority queue, waiter 0 raised to 0x60", SCE_KERNEL_ATTR_THREAD_PRIO,
        1);

    test_exit();
    return 0;
}
