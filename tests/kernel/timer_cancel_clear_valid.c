// What the generic event calls do to a timer, through its UID. A cancel ends the waiters of the
// timer but leaves the timer running, so it still fires when its time comes, and a cancel with a
// set pattern replaces every bit of the timer's pattern. Cases:
// - sceKernelCancelEvent on an auto reset timer with one waiter. The cancel ends the waiter with
//   the cancel result and a count of one, and the timer stays active. A second waiter that starts
//   after the cancel gets the event when the timer fires, one second after its start.
// - sceKernelCancelEventWithSetPattern on a manual reset timer with one waiter, with the set
//   pattern 0x01000000 and user data 0x77. The pattern and user data are the given ones until the
//   timer fires, which sets its bit on top of them and leaves the user data 0. A poll for the bit
//   fails until then.
// - the same cancel with the set pattern 0 and no waiter, which leaves the pattern 0.
// - a manual reset timer that fired, then a set of 0x01000000, then two clears with the masks
//   0xFF008000 and 0xFF000004. A clear refuses any mask with a bit in 0x00FFFFFF, so both are
//   refused with the illegal pattern result and the pattern stays as it was.
// Each case has a fresh timer. The timers of the first, second and last cases fire within one
// second, and the timer of the third never fires in the test. Every waiter is joined before the
// case prints anything, so no print falls between a timed wait and the timer's fire.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define ONE_SHOT_US 1000000
#define SHORT_ONE_SHOT_US 100000
#define LONG_INTERVAL_US 30000000
#define SET_PATTERN 0x01000000
#define SET_USER_DATA 0x77

// The timeouts of the two waiters of the first case, by the slot each one is started in
static const SceUInt32 timeouts[2] = { 5000000, 2500000 };

static SceUID timer;
static int wait_result[2];
static SceUInt32 wait_bits[2];
static SceUInt64 wait_user_data[2];

// Waits for the timer's bit with the timeout of its slot, and keeps what the wait returned
static int waiter(SceSize args, void *argp) {
    (void)args;
    const int slot = *(const int *)argp;
    SceUInt32 timeout = timeouts[slot];
    SceUInt32 bits = NOT_DONE;
    SceUInt64 user_data = NOT_DONE;
    wait_result[slot] = sceKernelWaitEvent(timer, EVENT_TIMER, &bits, &user_data, &timeout);
    wait_bits[slot] = bits;
    wait_user_data[slot] = user_data;
    return 0;
}

// An auto reset timer with one waiter, which a cancel ends. A second waiter then takes the event
// the timer fires.
static void auto_reset_case(void) {
    SceUInt64 interval = ONE_SHOT_US;
    timer = sceKernelCreateTimer("auto", EVENT_ATTR_AUTO_RESET, NULL);
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    sceKernelStartTimer(timer);

    const SceUID first = test_start_thread_arg(waiter, 0);
    TEST_WAIT_FOR(test_in_wait(first, WAIT_TYPE_EVENT, timer));
    SceUInt32 count = NOT_DONE;
    const int cancel = sceKernelCancelEvent(timer, &count);
    test_join_thread(first);
    EventInfo after_cancel = { .size = sizeof(after_cancel) };
    sceKernelGetEventInfo(timer, &after_cancel);
    TimerInfo timer_info = { .size = sizeof(timer_info) };
    sceKernelGetTimerInfo(timer, &timer_info);

    const SceUID second = test_start_thread_arg(waiter, 1);
    TEST_WAIT_FOR(test_in_wait(second, WAIT_TYPE_EVENT, timer));
    test_join_thread(second);

    PRINT("auto reset: the cancel result", cancel);
    PRINT("auto reset: the waiting count", count);
    PRINT("auto reset: the first waiter's result", wait_result[0]);
    PRINT("auto reset: the first waiter's result pattern", wait_bits[0]);
    PRINT("auto reset: the first waiter's user data, low word", (SceUInt32)wait_user_data[0]);
    PRINT("auto reset: the first waiter's user data, high word", (SceUInt32)(wait_user_data[0] >> 32));
    PRINT("auto reset: the pattern after the cancel", after_cancel.eventPattern);
    PRINT("auto reset: the timer is active after the cancel", timer_info.active);
    PRINT("auto reset: the second waiter's result", wait_result[1]);
    PRINT("auto reset: the second waiter's result pattern", wait_bits[1]);

    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

// A manual reset timer with one waiter, which a cancel with a set pattern and user data ends. The
// given pattern and user data stay until the timer fires.
static void manual_reset_case(void) {
    SceUInt64 interval = ONE_SHOT_US;
    timer = sceKernelCreateTimer("manual", EVENT_ATTR_MANUAL_RESET, NULL);
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    sceKernelStartTimer(timer);

    const SceUID thread = test_start_thread_arg(waiter, 0);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_EVENT, timer));
    SceUInt32 count = NOT_DONE;
    const int cancel = sceKernelCancelEventWithSetPattern(timer, SET_PATTERN, SET_USER_DATA, &count);
    test_join_thread(thread);
    EventInfo after_cancel = { .size = sizeof(after_cancel) };
    sceKernelGetEventInfo(timer, &after_cancel);
    TimerInfo timer_info = { .size = sizeof(timer_info) };
    sceKernelGetTimerInfo(timer, &timer_info);
    SceUInt32 poll_bits = NOT_DONE;
    SceUInt64 poll_user_data = NOT_DONE;
    const int poll_before = sceKernelPollEvent(timer, EVENT_TIMER, &poll_bits, &poll_user_data);

    // The poll doesn't clear a manual reset bit, so it only reads the bit until the timer fires
    TEST_WAIT_FOR(sceKernelPollEvent(timer, EVENT_TIMER, &poll_bits, &poll_user_data) == 0);
    EventInfo after_fire = { .size = sizeof(after_fire) };
    sceKernelGetEventInfo(timer, &after_fire);

    PRINT("manual reset: the cancel result", cancel);
    PRINT("manual reset: the waiting count", count);
    PRINT("manual reset: the waiter's result", wait_result[0]);
    PRINT("manual reset: the waiter's result pattern", wait_bits[0]);
    PRINT("manual reset: the waiter's user data, low word", (SceUInt32)wait_user_data[0]);
    PRINT("manual reset: the waiter's user data, high word", (SceUInt32)(wait_user_data[0] >> 32));
    PRINT("manual reset: the pattern after the cancel", after_cancel.eventPattern);
    PRINT("manual reset: the user data after the cancel", (SceUInt32)after_cancel.userData);
    PRINT("manual reset: the timer is active after the cancel", timer_info.active);
    PRINT("manual reset: a poll for 0x8000 before the fire", poll_before);
    PRINT("manual reset: the pattern after the fire", after_fire.eventPattern);
    PRINT("manual reset: the user data after the fire", (SceUInt32)after_fire.userData);

    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

// A timer with no waiter, cancelled with the set pattern 0, which leaves the pattern 0
static void zero_pattern_case(void) {
    SceUInt64 interval = LONG_INTERVAL_US;
    timer = sceKernelCreateTimer("zero", EVENT_ATTR_MANUAL_RESET, NULL);
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    sceKernelStartTimer(timer);

    EventInfo before = { .size = sizeof(before) };
    sceKernelGetEventInfo(timer, &before);
    SceUInt32 count = NOT_DONE;
    const int cancel = sceKernelCancelEventWithSetPattern(timer, 0, 0, &count);
    EventInfo after = { .size = sizeof(after) };
    sceKernelGetEventInfo(timer, &after);

    PRINT("zero set pattern: the pattern before the cancel", before.eventPattern);
    PRINT("zero set pattern: the cancel result", cancel);
    PRINT("zero set pattern: the waiting count", count);
    PRINT("zero set pattern: the pattern after the cancel", after.eventPattern);

    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

// A manual reset timer that fired, then a set of a simple event bit, then two clears
static void fired_case(void) {
    SceUInt64 interval = SHORT_ONE_SHOT_US;
    timer = sceKernelCreateTimer("fired", EVENT_ATTR_MANUAL_RESET, NULL);
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    sceKernelStartTimer(timer);

    SceUInt32 bits = NOT_DONE;
    SceUInt64 user_data = NOT_DONE;
    TEST_WAIT_FOR(sceKernelPollEvent(timer, EVENT_TIMER, &bits, &user_data) == 0);
    EventInfo fired = { .size = sizeof(fired) };
    sceKernelGetEventInfo(timer, &fired);

    const int set = sceKernelSetEvent(timer, EVENT_BIT_A, 0);
    EventInfo after_set = { .size = sizeof(after_set) };
    sceKernelGetEventInfo(timer, &after_set);

    const int clear_first = sceKernelClearEvent(timer, 0xFF008000);
    EventInfo after_first = { .size = sizeof(after_first) };
    sceKernelGetEventInfo(timer, &after_first);

    const int clear_second = sceKernelClearEvent(timer, 0xFF000004);
    EventInfo after_second = { .size = sizeof(after_second) };
    sceKernelGetEventInfo(timer, &after_second);

    PRINT("fired: the pattern after the fire", fired.eventPattern);
    PRINT("fired: the set result", set);
    PRINT("fired: the pattern after the set", after_set.eventPattern);
    PRINT("fired: the first clear result", clear_first);
    PRINT("fired: the pattern after the first clear", after_first.eventPattern);
    PRINT("fired: the second clear result", clear_second);
    PRINT("fired: the pattern after the second clear", after_second.eventPattern);

    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

int main(void) {
    auto_reset_case();
    manual_reset_case();
    zero_pattern_case();
    fired_case();

    test_exit();
    return 0;
}
