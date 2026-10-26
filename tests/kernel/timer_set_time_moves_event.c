// What SetTimerTime does to a one shot set event that is armed, on a running timer and on a stopped
// one, and to the remaining time and the info of the timer. Each case runs on a fresh manual reset
// timer with a one shot set event, and the main thread is the only thread of the test. The cases:
// - running, the event 4 s away, the time set to 3 s right after the start: the event is about 1 s
//   away from the set, so a wait with a 3 s timeout returns 0 between 500 ms and 1.5 s after it.
// - running, the event 2 s away, the time set to 0 after the timer ran for 1 s: the event is
//   2 s away from the set, so a wait with a 4 s timeout returns 0 between 1.5 s and 2.5 s after it.
// - running, the event 2 s away, the time set to 5 s right after the start, past the event's time:
//   a wait with a 4 s timeout returns 0 between 1.5 s and 2.5 s after the set.
// - stopped, the event 4 s away, the time set to 1.5 s: the remaining time is 2.5 s, and the info
//   shows the schedule at 4 s and the current and base times at 1.5 s. Set to 5 s, the remaining
//   time call still succeeds, and its value is above 0x8000000000000000, 1 s below 0 as a wrapped
//   value.
// - running, the event 30 s away, the time set to 10 s: the remaining time plus the timer's time
//   is the schedule, within 50 ms. Set to 40 s, past the schedule, the remaining time call still
//   succeeds, and its value is 10 s below 0 as a wrapped value.
// Times are only printed as comparisons, and each threshold is at least 250 ms from every outcome
// it tells apart.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// Creates a manual reset timer, and returns it
static SceUID create_timer(void) {
    return sceKernelCreateTimer("set time", EVENT_ATTR_MANUAL_RESET, NULL);
}

// Sets a one shot set event of the interval on the timer, and returns the call's result
static int set_event(SceUID timer, SceUInt64 interval) {
    SceUInt64 interval_us = interval;
    return sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval_us, 0);
}

// Waits for the timer's bit with a timeout, and returns the wait's result
static int wait_for_bit(SceUID timer, SceUInt32 timeout_us) {
    SceUInt32 bits = 0;
    SceUInt64 user_data = 0;
    SceUInt32 timeout = timeout_us;
    return sceKernelWaitEvent(timer, EVENT_TIMER, &bits, &user_data, &timeout);
}

// The event is 4 s away, and the time is set to 3 s right after the start, so it is 1 s away
static void moved_earlier_case(void) {
    const SceUID timer = create_timer();
    PRINT("a: the set of the event, 4 s away", set_event(timer, 4000000));
    PRINT("a: start", sceKernelStartTimer(timer));
    SceKernelSysClock time = 3000000;
    const SceUInt64 set = sceKernelGetProcessTimeWide();
    const int set_time = sceKernelSetTimerTime(timer, &time);
    const int wait = wait_for_bit(timer, 3000000);
    const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - set;
    PRINT("a: the set of the time to 3 s", set_time);
    PRINT("a: the wait with a 3 s timeout", wait);
    PRINT("a: it returned over 500 ms after the set", elapsed > 500000);
    PRINT("a: it returned under 1.5 s after the set", elapsed < 1500000);
    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

// The event is 2 s away, and after the timer ran for 1 s the time is set to 0, so the event is
// 2 s away from the set and not 1 s away
static void moved_later_case(void) {
    const SceUID timer = create_timer();
    PRINT("b: the set of the event, 2 s away", set_event(timer, 2000000));
    PRINT("b: start", sceKernelStartTimer(timer));
    sceKernelDelayThread(1000000);
    SceKernelSysClock time = 0;
    const SceUInt64 set = sceKernelGetProcessTimeWide();
    const int set_time = sceKernelSetTimerTime(timer, &time);
    const int wait = wait_for_bit(timer, 4000000);
    const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - set;
    PRINT("b: the set of the time to 0", set_time);
    PRINT("b: the wait with a 4 s timeout", wait);
    PRINT("b: it returned over 1.5 s after the set", elapsed > 1500000);
    PRINT("b: it returned under 2.5 s after the set", elapsed < 2500000);
    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

// The event is 2 s away, and the time is set to 5 s right after the start, past the event's time
static void past_schedule_case(void) {
    const SceUID timer = create_timer();
    PRINT("c: the set of the event, 2 s away", set_event(timer, 2000000));
    PRINT("c: start", sceKernelStartTimer(timer));
    SceKernelSysClock time = 5000000;
    const SceUInt64 set = sceKernelGetProcessTimeWide();
    const int set_time = sceKernelSetTimerTime(timer, &time);
    const int wait = wait_for_bit(timer, 4000000);
    const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - set;
    PRINT("c: the set of the time to 5 s", set_time);
    PRINT("c: the wait with a 4 s timeout", wait);
    PRINT("c: it returned over 1.5 s after the set", elapsed > 1500000);
    PRINT("c: it returned under 2.5 s after the set", elapsed < 2500000);
    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

// The timer is stopped with the event 4 s away, and the time is set to 1.5 s, then to 5 s
static void stopped_case(void) {
    const SceUID timer = create_timer();
    PRINT("d: the set of the event, 4 s away", set_event(timer, 4000000));
    SceKernelSysClock time = 1500000;
    const int set_time = sceKernelSetTimerTime(timer, &time);
    SceKernelSysClock left = 0;
    const int remaining = sceKernelGetTimerEventRemainingTime(timer, &left);
    TimerInfo info = { .size = sizeof(info) };
    const int info_result = sceKernelGetTimerInfo(timer, &info);
    PRINT("d: the set of the time to 1.5 s", set_time);
    PRINT("d: the remaining time", remaining);
    PRINT("d: the remaining time is 2.5 s", left == 2500000);
    PRINT("d: the info call", info_result);
    PRINT("d: the info schedule is 4 s", info.schedule == 4000000);
    PRINT("d: the info current time is 1.5 s", info.currentTime == 1500000);
    PRINT("d: the info base time is 1.5 s", info.baseTime == 1500000);

    time = 5000000;
    PRINT("d: the set of the time to 5 s", sceKernelSetTimerTime(timer, &time));
    left = 0;
    PRINT("d: the remaining time after that", sceKernelGetTimerEventRemainingTime(timer, &left));
    PRINT("d: the remaining time is above 0x8000000000000000", left > 0x8000000000000000ULL);
    PRINT("d: 0 minus the remaining time is exactly 1 s", (SceUInt64)0 - left == 1000000);
    sceKernelDeleteTimer(timer);
}

// The event is 30 s away on a running timer, and the time is set to 10 s, then to 40 s, past it
static void far_schedule_case(void) {
    const SceUID timer = create_timer();
    PRINT("e: the set of the event, 30 s away", set_event(timer, 30000000));
    PRINT("e: start", sceKernelStartTimer(timer));
    SceKernelSysClock time = 10000000;
    const int set_time = sceKernelSetTimerTime(timer, &time);
    const SceUInt64 now = sceKernelGetTimerTimeWide(timer);
    SceKernelSysClock left = 0;
    const int remaining = sceKernelGetTimerEventRemainingTime(timer, &left);
    const SceInt64 off = (SceInt64)(left + now) - 30000000;
    PRINT("e: the set of the time to 10 s", set_time);
    PRINT("e: the remaining time", remaining);
    PRINT("e: the remaining time plus the timer's time is the schedule, within 50 ms",
          off > -50000 && off < 50000);

    time = 40000000;
    PRINT("e: the set of the time to 40 s", sceKernelSetTimerTime(timer, &time));
    left = 0;
    const int wrapped_result = sceKernelGetTimerEventRemainingTime(timer, &left);
    const SceUInt64 below_zero = (SceUInt64)0 - left;
    PRINT("e: the remaining time after that", wrapped_result);
    PRINT("e: 0 minus the remaining time is within 9.75 to 10.25 s",
          below_zero > 9750000 && below_zero < 10250000);
    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

int main(void) {
    moved_earlier_case();
    moved_later_case();
    past_schedule_case();
    stopped_case();
    far_schedule_case();

    test_exit();
    return 0;
}
