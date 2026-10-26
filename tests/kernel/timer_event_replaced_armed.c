// SetTimerEvent on a running timer that already has an event armed: for an event that comes later,
// for a one shot event that replaces a repeating one, and for a change of the event's type. The old
// event is unlinked in each case and the new one takes its place. Each case runs on a fresh timer,
// and the main thread is the only thread of the test. The cases:
// - later: an auto reset timer with a one shot set event of 1.5 s is started, and at 0.5 s a one
//   shot set event of 3 s is set on it. A wait for the timer's bit with a 5 s timeout returns 0,
//   and it returned over 2 s and under 4 s after the set. The old event would come 1 s after it.
// - repeat to one shot: an auto reset timer with a repeating set event of 1 s is started, and after
//   the first wait returns, a one shot set event of 1.5 s is set on it. The info of the timer shows
//   the repeat setting and the interval. The next wait with a 3 s timeout returns 0, over 1.25 s
//   and under 2 s after the set, and a wait with a 2.5 s timeout after it times out, since no
//   repeat is left.
// - type change from set to pulse: a manual reset timer with a one shot set event of 1 s is
//   started, and at 0.5 s a one shot pulse event of 1 s replaces it. The wait returns 0 with the
//   bit 0x8000, and a poll right after the wait fails, since a pulse leaves the bit clear.
// - type change from pulse to set: the same, with a one shot pulse of 1 s replaced at 0.5 s by a
//   one shot set event of 1 s. The wait returns 0 with the bit 0x8000, and a poll right after the
//   wait succeeds, since a set leaves the bit set on a manual reset timer.
// Times are only printed as comparisons, and each threshold is at least 250 ms from every outcome
// it tells apart.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// Creates a timer with the reset attribute, and returns it
static SceUID create_timer(SceUInt32 attr) {
    return sceKernelCreateTimer("replaced", attr, NULL);
}

// Sets a one shot event of the type and interval on the timer, and returns the call's result
static int set_event(SceUID timer, int type, SceUInt64 interval) {
    SceUInt64 interval_us = interval;
    return sceKernelSetTimerEvent(timer, type, &interval_us, 0);
}

// Waits for the timer's bit with a timeout, sets bits to what the wait got, and returns its result
static int wait_for_bit(SceUID timer, SceUInt32 timeout_us, SceUInt32 *bits) {
    SceUInt64 user_data = 0;
    SceUInt32 timeout = timeout_us;
    return sceKernelWaitEvent(timer, EVENT_TIMER, bits, &user_data, &timeout);
}

// Polls the timer's bit, and returns the call's result
static int poll_event(SceUID timer) {
    SceUInt32 bits = 0;
    SceUInt64 user_data = 0;
    return sceKernelPollEvent(timer, EVENT_TIMER, &bits, &user_data);
}

// Returns the schedule of the timer, in timer time, from its info
static SceUInt64 schedule_of(SceUID timer) {
    TimerInfo info = { .size = sizeof(info) };
    sceKernelGetTimerInfo(timer, &info);
    return info.schedule;
}

// Sleeps until the process time reaches until, in microseconds
static void sleep_until(SceUInt64 until) {
    while (sceKernelGetProcessTimeWide() < until)
        sceKernelDelayThread(1000);
}

// A one shot set event of 1.5 s is replaced by one of 3 s at 0.5 s
static void later_case(void) {
    const SceUID timer = create_timer(EVENT_ATTR_AUTO_RESET);
    PRINT("later: the set of the first event, a one shot of 1.5 s",
          set_event(timer, TIMER_TYPE_SET_EVENT, 1500000));
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    PRINT("later: start", sceKernelStartTimer(timer));
    sleep_until(start + 500000);
    const SceUInt64 set = sceKernelGetProcessTimeWide();
    const int set_later = set_event(timer, TIMER_TYPE_SET_EVENT, 3000000);
    SceUInt32 bits = 0;
    const int wait = wait_for_bit(timer, 5000000, &bits);
    const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - set;
    PRINT("later: the set of the replacement, a one shot of 3 s, at 0.5 s", set_later);
    PRINT("later: the wait with a 5 s timeout", wait);
    PRINT("later: it returned over 2 s after the set", elapsed > 2000000);
    PRINT("later: it returned under 4 s after the set", elapsed < 4000000);
    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

// A repeating set event of 1 s is replaced by a one shot set event of 1.5 s after its first fire
static void repeat_case(void) {
    const SceUID timer = create_timer(EVENT_ATTR_AUTO_RESET);
    SceUInt64 interval = 1000000;
    PRINT("repeat: the set of a repeating event of 1 s",
          sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 1));
    PRINT("repeat: start", sceKernelStartTimer(timer));
    SceUInt32 bits = 0;
    const int first = wait_for_bit(timer, 3000000, &bits);
    // The schedule can move on a little after the wake, so wait until it has moved past the fire
    TEST_WAIT_FOR(schedule_of(timer) > 1000000);
    const SceUInt64 set = sceKernelGetProcessTimeWide();
    const int set_one_shot = set_event(timer, TIMER_TYPE_SET_EVENT, 1500000);
    TimerInfo info = { .size = sizeof(info) };
    const int info_result = sceKernelGetTimerInfo(timer, &info);
    const int second = wait_for_bit(timer, 3000000, &bits);
    const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - set;
    const int third = wait_for_bit(timer, 2500000, &bits);
    PRINT("repeat: the first wait with a 3 s timeout", first);
    PRINT("repeat: the set of a one shot event of 1.5 s, after the first wait", set_one_shot);
    PRINT("repeat: the info call", info_result);
    PRINT("repeat: the info repeat setting", info.repeat);
    PRINT("repeat: the info interval is 1.5 s", info.interval == 1500000);
    PRINT("repeat: the second wait with a 3 s timeout", second);
    PRINT("repeat: it returned over 1.25 s after the set", elapsed > 1250000);
    PRINT("repeat: it returned under 2 s after the set", elapsed < 2000000);
    PRINT("repeat: the third wait with a 2.5 s timeout", third);
    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

// A one shot set event of 1 s is replaced by a one shot pulse of 1 s at 0.5 s
static void set_to_pulse_case(void) {
    const SceUID timer = create_timer(EVENT_ATTR_MANUAL_RESET);
    PRINT("set to pulse: the set of a one shot set event of 1 s",
          set_event(timer, TIMER_TYPE_SET_EVENT, 1000000));
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    PRINT("set to pulse: start", sceKernelStartTimer(timer));
    sleep_until(start + 500000);
    const int replaced = set_event(timer, TIMER_TYPE_PULSE_EVENT, 1000000);
    TimerInfo info = { .size = sizeof(info) };
    const int info_result = sceKernelGetTimerInfo(timer, &info);
    SceUInt32 bits = 0;
    const int wait = wait_for_bit(timer, 3000000, &bits);
    const int poll = poll_event(timer);
    PRINT("set to pulse: the set of a one shot pulse of 1 s, at 0.5 s", replaced);
    PRINT("set to pulse: the info call", info_result);
    PRINT("set to pulse: the info type", info.type);
    PRINT("set to pulse: the wait with a 3 s timeout", wait);
    if (wait == 0)
        PRINT("set to pulse: the bit the wait got", bits);
    PRINT("set to pulse: a poll right after the wait", poll);
    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

// A one shot pulse of 1 s is replaced by a one shot set event of 1 s at 0.5 s
static void pulse_to_set_case(void) {
    const SceUID timer = create_timer(EVENT_ATTR_MANUAL_RESET);
    PRINT("pulse to set: the set of a one shot pulse of 1 s",
          set_event(timer, TIMER_TYPE_PULSE_EVENT, 1000000));
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    PRINT("pulse to set: start", sceKernelStartTimer(timer));
    sleep_until(start + 500000);
    const int replaced = set_event(timer, TIMER_TYPE_SET_EVENT, 1000000);
    TimerInfo info = { .size = sizeof(info) };
    const int info_result = sceKernelGetTimerInfo(timer, &info);
    SceUInt32 bits = 0;
    const int wait = wait_for_bit(timer, 3000000, &bits);
    const int poll = poll_event(timer);
    PRINT("pulse to set: the set of a one shot set event of 1 s, at 0.5 s", replaced);
    PRINT("pulse to set: the info call", info_result);
    PRINT("pulse to set: the info type", info.type);
    PRINT("pulse to set: the wait with a 3 s timeout", wait);
    if (wait == 0)
        PRINT("pulse to set: the bit the wait got", bits);
    PRINT("pulse to set: a poll right after the wait", poll);
    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

int main(void) {
    later_case();
    repeat_case();
    set_to_pulse_case();
    pulse_to_set_case();

    test_exit();
    return 0;
}
