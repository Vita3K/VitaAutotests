// An AND sceKernelWaitMultipleEvents on a timer and a simple event, both manual reset. The timer is
// event 1 and the simple event is event 2. Each case gets fresh objects. It prints the wait's
// result, then each entry it filled with its event, result, bits and the two halves of its user
// data, and the time left as a comparison. Cases:
// - a: the timer starts with a 50 ms interval, and the simple event is set 600 ms into the wait
// - b: the timer starts and is given 10 times its interval to fire, the simple event is set, then
//   the wait returns at once
// - c: the timer starts and stops before it fires, the simple event is set, and the wait times out
//   in 300 ms
// - d: the timer starts with a 2 s interval, a waiter queues on both events, the timer is canceled
//   while the wait is queued, then the simple event is set
// The model is multi_events_waits, for its timer case, and multi_events_and_delete for the AND
// entries. No callbacks are involved.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// The timer is events[0], which is event 1, and the simple event events[1], which is event 2
static SceUID timer, event;
static WaitEvent events[2];
static ResultEvent results[2];
static SceUInt32 wait_timeout;
static volatile int wait_result;

// The user data the simple event is set with, with both halves set
#define EVENT_USER_DATA 0x1122334455667788ULL

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    wait_result = sceKernelWaitMultipleEvents(events, 2, EVENT_WAIT_MODE_AND, results, &wait_timeout);
    return 0;
}

// Creates a manual reset timer with a repeat of 0 and the given interval in microseconds, and a
// manual reset simple event, and fills the wait's events with them
static void setup(SceUInt64 interval) {
    timer = sceKernelCreateTimer("timer", EVENT_ATTR_MANUAL_RESET, NULL);
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    events[0] = (WaitEvent){ timer, EVENT_TIMER };
    events[1] = (WaitEvent){ event, EVENT_BIT_A };
    sceClibMemset(results, 0xAA, sizeof(results));
}

static void teardown(void) {
    sceKernelDeleteTimer(timer);
    sceKernelDeleteSimpleEvent(event);
}

// Returns 1 for the timer, 2 for the simple event, or 0 for another UID
static int which_event(SceUID id) {
    if (id == timer)
        return 1;
    return id == event ? 2 : 0;
}

// Prints the wait's result, then the entries it filled
static void print_wait(const char *name, int count) {
    PRINTF(count, "%s: the wait", name);
    for (int i = 0; i < 2 && i < count; i++) {
        PRINTF(which_event(results[i].eventId), "%s: result %d is for the timer (1) or the event (2)", name, i);
        PRINTF(results[i].result, "%s: result %d", name, i);
        PRINTF(results[i].resultPattern, "%s: result %d bits", name, i);
        PRINTF((int)(results[i].userData >> 32), "%s: result %d user data high half", name, i);
        PRINTF((int)results[i].userData, "%s: result %d user data low half", name, i);
    }
}

int main(void) {
    SceUID thread;
    SceUInt32 timeout, waiters;
    int count;

    // a: the timer fires at 50 ms, and the event is set 600 ms into the wait
    setup(50000);
    PRINT("a: start the timer", sceKernelStartTimer(timer));
    wait_timeout = TEST_WAIT_US;
    thread = test_start_thread(waiter);
    test_wait_until_waiting(thread);
    sceKernelDelayThread(600000);
    PRINT("a: set the event", sceKernelSetEvent(event, EVENT_BIT_A, EVENT_USER_DATA));
    test_join_thread(thread);
    print_wait("a", wait_result);
    PRINT("a: the time left is over 1 s", wait_timeout > 1000000);
    teardown();

    // b: the timer has 10 times its interval to fire, and the event is set before the wait
    setup(50000);
    PRINT("b: start the timer", sceKernelStartTimer(timer));
    sceKernelDelayThread(500000);
    PRINT("b: set the event", sceKernelSetEvent(event, EVENT_BIT_A, EVENT_USER_DATA));
    timeout = 2000000;
    count = sceKernelWaitMultipleEvents(events, 2, EVENT_WAIT_MODE_AND, results, &timeout);
    print_wait("b", count);
    PRINT("b: the time left is over 1 s", timeout > 1000000);
    teardown();

    // c: the timer starts and stops before it fires, the event is set, and the wait times out
    setup(1000000);
    PRINT("c: start the timer", sceKernelStartTimer(timer));
    PRINT("c: stop the timer", sceKernelStopTimer(timer));
    PRINT("c: set the event", sceKernelSetEvent(event, EVENT_BIT_A, EVENT_USER_DATA));
    timeout = 300000;
    count = sceKernelWaitMultipleEvents(events, 2, EVENT_WAIT_MODE_AND, results, &timeout);
    print_wait("c", count);
    PRINT("c: the time left is 0", timeout == 0);
    teardown();

    // d: the timer is canceled while the wait is queued, then the event is set
    setup(2000000);
    PRINT("d: start the timer", sceKernelStartTimer(timer));
    wait_timeout = 5000000;
    thread = test_start_thread(waiter);
    test_wait_until_waiting(thread);
    waiters = 0xDEAD;
    PRINT("d: cancel the timer", sceKernelCancelTimer(timer, &waiters));
    PRINT("d: threads the cancel found waiting", waiters);
    PRINT("d: set the event", sceKernelSetEvent(event, EVENT_BIT_A, EVENT_USER_DATA));
    test_join_thread(thread);
    print_wait("d", wait_result);
    PRINT("d: the time left is over 1 s", wait_timeout > 1000000);
    teardown();

    test_exit();
    return 0;
}
