// What a woken simple event waiter gets, and whether a later call on the event before the waiter
// runs changes it. Every thread is pinned to core 0. The main thread runs at 0x60 and the waiters
// at 0xB0, so a woken waiter does not run until the main thread blocks, and the main thread makes
// its other calls on the event first. Waiter i waits for the bits given to start_waiter, and the
// waiters use a timeout of three times TEST_WAIT_US. The cases:
// - manual reset: W0 waits for A. The main thread sets A with user data 0x11, then sets B with
//   0x22, clears the event, and pulses C with 0x33.
// - auto reset, W0 and W1 both waiting for A: a set of A with 0x11, then a set of A with 0x22.
// - auto reset, W0 and W1 both waiting for A: a pulse of A with 0x11, then a set of A with 0x22.
// - manual reset, W0 waiting for A and W1 for B: a set of A with 0x11, then a set of B with 0x22.
// Right after the first call of a case, the main thread reads how many threads wait on the event
// and whether W0 still waits on it. The exact thread status is not read, since a woken waiter can
// start running before the main thread reads it. The main thread prints only after the waiters
// have ended. Then each case prints the result of its calls, each waiter's result, its bits masked
// to the user bits and the low word of its user data, the bits left on the event, and what a final
// poll for any user bit gets.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
// The bits a game may use on a simple event
#define USER_BITS 0xFF000000

static SceUID event;
static SceUID threads[2];
static volatile SceUInt32 wanted[2];
static volatile int results[2];
static volatile SceUInt32 bits[2];
static volatile SceUInt64 user_data[2];

// Waits for the bits of waiter i, and keeps what the wait gave it
static int waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt32 pattern = NOT_DONE;
    SceUInt64 data = NOT_DONE;
    SceUInt timeout = 3 * TEST_WAIT_US;
    results[i] = sceKernelWaitEvent(event, wanted[i], &pattern, &data, &timeout);
    bits[i] = pattern;
    user_data[i] = data;
    return 0;
}

// Starts waiter i for pattern, and returns once it waits on the event
static void start_waiter(int i, SceUInt32 pattern) {
    wanted[i] = pattern;
    results[i] = NOT_DONE;
    bits[i] = NOT_DONE;
    user_data[i] = NOT_DONE;
    threads[i] = sceKernelCreateThread(
        "waiter", waiter, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(threads[i], sizeof(i), &i);
    TEST_WAIT_FOR(test_in_wait(threads[i], WAIT_TYPE_EVENT, event));
}

// Returns how many threads wait on the event, or NOT_DONE if the call fails
static int waiting_on_event(void) {
    EventInfo info = { .size = sizeof(info) };
    return sceKernelGetEventInfo(event, &info) == 0 ? (int)info.numWaitThreads : NOT_DONE;
}

// Waits until the first count waiters have ended
static void join_waiters(int count) {
    for (int i = 0; i < count; i++)
        test_join_thread(threads[i]);
}

// Prints what the first count waiters got, then what is left on the event and what a final poll
// for any user bit gets, and deletes the event
static void print_outcome(const char *name, int count) {
    for (int i = 0; i < count; i++) {
        PRINTF(results[i], "%s: waiter %d's result", name, i);
        PRINTF(bits[i] & USER_BITS, "%s: the bits waiter %d got", name, i);
        PRINTF((SceUInt32)user_data[i], "%s: the user data waiter %d got", name, i);
    }
    SceUInt32 left = NOT_DONE;
    sceKernelGetEventPattern(event, &left);
    SceUInt32 polled = NOT_DONE;
    SceUInt64 polled_data = NOT_DONE;
    const int poll = sceKernelPollEvent(event, USER_BITS, &polled, &polled_data);
    PRINTF(left & USER_BITS, "%s: the bits left", name);
    PRINTF(poll, "%s: the final poll", name);
    PRINTF(polled & USER_BITS, "%s: the bits the final poll got", name);
    PRINTF((SceUInt32)polled_data, "%s: the user data the final poll got", name);
    sceKernelDeleteSimpleEvent(event);
}

// A manual reset event with W0 waiting for A. Before W0 runs, the main thread sets B, clears the
// event and pulses C.
static void manual_case(void) {
    const char *name = "manual reset";
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    start_waiter(0, EVENT_BIT_A);
    const int set_a = sceKernelSetEvent(event, EVENT_BIT_A, 0x11);
    const int waiting = waiting_on_event();
    const int still_waiting = test_in_wait(threads[0], WAIT_TYPE_EVENT, event);
    const int set_b = sceKernelSetEvent(event, EVENT_BIT_B, 0x22);
    const int clear = sceKernelClearEvent(event, 0);
    const int pulse_c = sceKernelPulseEvent(event, EVENT_BIT_C, 0x33);
    join_waiters(1);

    PRINTF(set_a, "%s: the set of A", name);
    PRINTF(waiting, "%s: the threads waiting after the set of A", name);
    PRINTF(still_waiting, "%s: W0 still waits after the set of A", name);
    PRINTF(set_b, "%s: the set of B", name);
    PRINTF(clear, "%s: the clear", name);
    PRINTF(pulse_c, "%s: the pulse of C", name);
    print_outcome(name, 1);
}

// An auto reset event with W0 and W1 both waiting for A, and two sets of A before either runs
static void auto_sets_case(void) {
    const char *name = "auto reset, two sets of A";
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL);
    start_waiter(0, EVENT_BIT_A);
    start_waiter(1, EVENT_BIT_A);
    const int first = sceKernelSetEvent(event, EVENT_BIT_A, 0x11);
    const int waiting = waiting_on_event();
    const int still_waiting = test_in_wait(threads[0], WAIT_TYPE_EVENT, event);
    const int second = sceKernelSetEvent(event, EVENT_BIT_A, 0x22);
    join_waiters(2);

    PRINTF(first, "%s: the first set", name);
    PRINTF(waiting, "%s: the threads waiting after the first set", name);
    PRINTF(still_waiting, "%s: W0 still waits after the first set", name);
    PRINTF(second, "%s: the second set", name);
    print_outcome(name, 2);
}

// An auto reset event with W0 and W1 both waiting for A, a pulse of A and then a set of A
static void auto_pulse_then_set_case(void) {
    const char *name = "auto reset, a pulse then a set of A";
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL);
    start_waiter(0, EVENT_BIT_A);
    start_waiter(1, EVENT_BIT_A);
    const int pulse = sceKernelPulseEvent(event, EVENT_BIT_A, 0x11);
    const int waiting = waiting_on_event();
    const int still_waiting = test_in_wait(threads[0], WAIT_TYPE_EVENT, event);
    const int set = sceKernelSetEvent(event, EVENT_BIT_A, 0x22);
    join_waiters(2);

    PRINTF(pulse, "%s: the pulse", name);
    PRINTF(waiting, "%s: the threads waiting after the pulse", name);
    PRINTF(still_waiting, "%s: W0 still waits after the pulse", name);
    PRINTF(set, "%s: the set", name);
    print_outcome(name, 2);
}

// A manual reset event with W0 waiting for A and W1 for B, and a set of A then a set of B
static void manual_two_bits_case(void) {
    const char *name = "manual reset, A for W0 and B for W1";
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    start_waiter(0, EVENT_BIT_A);
    start_waiter(1, EVENT_BIT_B);
    const int set_a = sceKernelSetEvent(event, EVENT_BIT_A, 0x11);
    const int waiting = waiting_on_event();
    const int still_waiting = test_in_wait(threads[0], WAIT_TYPE_EVENT, event);
    const int set_b = sceKernelSetEvent(event, EVENT_BIT_B, 0x22);
    join_waiters(2);

    PRINTF(set_a, "%s: the set of A", name);
    PRINTF(waiting, "%s: the threads waiting after the set of A", name);
    PRINTF(still_waiting, "%s: W0 still waits after the set of A", name);
    PRINTF(set_b, "%s: the set of B", name);
    print_outcome(name, 2);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    manual_case();
    auto_sets_case();
    auto_pulse_then_set_case();
    manual_two_bits_case();

    test_exit();
    return 0;
}
