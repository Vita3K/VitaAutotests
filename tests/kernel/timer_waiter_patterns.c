// Which bits the waits and polls on one timer get when threads wait for different bits. The three
// waiters, W0, W1 and W2, wait on core 0 in that order, each once the one before it waits, and the
// main thread, on core 1, starts the timer once they queue. The cases:
// - manual reset, waiters queued before the start. A one shot set event of 300 ms is set before
//   the start. Before the start, a poll for the timer bit fails, and a poll for the timer bit and
//   the create bit gets the create bit. W0 waits for a user bit, W1 for the timer bit and W2 for the
//   timer bit and the user bit. The fire wakes W1 and W2 with the timer bit, and W0 is the one
//   waiter left. Setting the user bit with user data 0x55 wakes W0 with that bit and the data, and
//   the timer keeps its bits set.
// - auto reset, waiters queued before the start, with a one shot set event of 300 ms. W0 waits for
//   a user bit, W1 for the timer bit and the user bit, and W2 for the timer bit. The fire is taken
//   by W1, so the walk goes on and W2 misses it. W0 and W2 are still waiting when the timer is
//   cancelled, which counts 2 and ends both.
// - auto reset with no waiter, and a one shot set event of 100 ms. A poll for the create bit takes
//   it, and a second poll fails. The timer starts, and a poll 600 ms later gets the timer bit only.
//   Polls of a manual reset timer for the create bit all succeed.
// The results, the bits the waits and polls got, the user data, the waiting counts and the
// patterns print as numbers, including the bits a call that fails gets.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define CREATE_BIT 0x4
#define FIRE_US 300000
#define CONSUME_FIRE_US 100000
#define POLL_AFTER_START_US 600000
#define WAITERS 3

static SceUID timer;
static SceUID threads[WAITERS];
static SceUInt32 wanted[WAITERS];
static volatile SceInt32 results[WAITERS];
static volatile SceUInt32 bits[WAITERS];
static volatile SceUInt64 user_data[WAITERS];

static int waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt32 got = NOT_DONE;
    SceUInt64 data = 0;
    SceUInt32 timeout = 3 * TEST_WAIT_US;
    results[i] = sceKernelWaitEvent(timer, wanted[i], &got, &data, &timeout);
    bits[i] = got;
    user_data[i] = data;
    return 0;
}

// Starts waiter i waiting for pattern on the timer, and returns once it is in the wait
static void start_waiter(int i, SceUInt32 pattern) {
    static const int priorities[WAITERS] = { 0x90, 0x70, 0x80 };
    wanted[i] = pattern;
    results[i] = NOT_DONE;
    bits[i] = NOT_DONE;
    user_data[i] = 0;
    threads[i] = sceKernelCreateThread(
        "waiter", waiter, priorities[i], 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(threads[i], sizeof(i), &i);
    TEST_WAIT_FOR(test_in_wait(threads[i], WAIT_TYPE_EVENT, timer));
}

// Polls the timer for pattern, and prints the result and the bits the poll got
static void poll_case(const char *name, SceUInt32 pattern) {
    SceUInt32 got = NOT_DONE;
    SceUInt64 data = 0;
    PRINTF(sceKernelPollEvent(timer, pattern, &got, &data), "%s: the poll", name);
    PRINTF(got, "%s: the bits it got", name);
}

// Prints the timer's info call, its waiting threads and its pattern, under the label when
static void print_state(const char *when) {
    EventInfo info = { .size = sizeof(info) };
    PRINTF(sceKernelGetEventInfo(timer, &info), "%s: the info call", when);
    PRINTF(info.numWaitThreads, "%s: the waiting threads", when);
    PRINTF(info.eventPattern, "%s: the pattern", when);
}

// Prints waiter i's result, and the bits and user data it got, under the label name
static void print_waiter(const char *name, int i) {
    PRINTF(results[i], "%s: waiter %d's result", name, i);
    PRINTF(bits[i], "%s: the bits waiter %d got", name, i);
    PRINTF(user_data[i], "%s: the user data waiter %d got", name, i);
}

// A manual reset timer, with its waiters queued before the start
static void manual_reset_case(void) {
    timer = sceKernelCreateTimer("manual", EVENT_ATTR_MANUAL_RESET, NULL);
    SceUInt64 interval = FIRE_US;
    PRINT("manual reset: set a one shot set event of 300 ms",
          sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0));
    poll_case("manual reset, before the start: a poll for the timer bit", EVENT_TIMER);
    poll_case("manual reset, before the start: a poll for the timer bit and the create bit",
              EVENT_TIMER | CREATE_BIT);

    start_waiter(0, EVENT_BIT_A);
    start_waiter(1, EVENT_TIMER);
    start_waiter(2, EVENT_TIMER | EVENT_BIT_A);
    PRINT("manual reset: start the timer", sceKernelStartTimer(timer));
    test_join_thread(threads[1]);
    test_join_thread(threads[2]);
    print_waiter("manual reset, after the fire", 1);
    print_waiter("manual reset, after the fire", 2);
    print_state("manual reset, after the fire");

    PRINT("manual reset: set the user bit with user data 0x55",
          sceKernelSetEvent(timer, EVENT_BIT_A, 0x55));
    test_join_thread(threads[0]);
    print_waiter("manual reset, after the user bit", 0);
    print_state("manual reset, after the user bit");

    PRINT("manual reset: stop the timer", sceKernelStopTimer(timer));
    sceKernelDeleteTimer(timer);
}

// An auto reset timer, with its waiters queued before the start
static void auto_reset_case(void) {
    timer = sceKernelCreateTimer("auto", EVENT_ATTR_AUTO_RESET, NULL);
    SceUInt64 interval = FIRE_US;
    PRINT("auto reset: set a one shot set event of 300 ms",
          sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0));

    start_waiter(0, EVENT_BIT_A);
    start_waiter(1, EVENT_TIMER | EVENT_BIT_A);
    start_waiter(2, EVENT_TIMER);
    PRINT("auto reset: start the timer", sceKernelStartTimer(timer));
    test_join_thread(threads[1]);
    print_waiter("auto reset, after the fire", 1);
    print_state("auto reset, after the fire");

    SceUInt32 counted = NOT_DONE;
    PRINT("auto reset: cancel the timer", sceKernelCancelTimer(timer, &counted));
    PRINT("auto reset: the waiters the cancel counted", counted);
    test_join_thread(threads[0]);
    test_join_thread(threads[2]);
    print_waiter("auto reset, after the cancel", 0);
    print_waiter("auto reset, after the cancel", 2);

    PRINT("auto reset: stop the timer", sceKernelStopTimer(timer));
    sceKernelDeleteTimer(timer);
}

// Polls that take the create bit of an auto reset timer, and the same polls of a manual reset timer
static void consume_case(void) {
    timer = sceKernelCreateTimer("consume", EVENT_ATTR_AUTO_RESET, NULL);
    SceUInt64 interval = CONSUME_FIRE_US;
    PRINT("auto reset, no waiter: set a one shot set event of 100 ms",
          sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0));
    poll_case("auto reset, no waiter: the first poll for the create bit", CREATE_BIT);
    print_state("auto reset, no waiter: after the first poll");
    poll_case("auto reset, no waiter: the second poll for the create bit", CREATE_BIT);

    PRINT("auto reset, no waiter: start the timer", sceKernelStartTimer(timer));
    sceKernelDelayThread(POLL_AFTER_START_US);
    poll_case("auto reset, no waiter: the poll after the fire", EVENT_TIMER | CREATE_BIT);
    PRINT("auto reset, no waiter: stop the timer", sceKernelStopTimer(timer));
    sceKernelDeleteTimer(timer);

    timer = sceKernelCreateTimer("keep", EVENT_ATTR_MANUAL_RESET, NULL);
    poll_case("manual reset, no waiter: the first poll for the create bit", CREATE_BIT);
    poll_case("manual reset, no waiter: the second poll for the create bit", CREATE_BIT);
    poll_case("manual reset, no waiter: the third poll for the create bit", CREATE_BIT);
    sceKernelDeleteTimer(timer);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    manual_reset_case();
    auto_reset_case();
    consume_case();

    test_exit();
    return 0;
}
