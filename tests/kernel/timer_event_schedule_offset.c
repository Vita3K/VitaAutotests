// The schedule a set of a timer event gives. It is the timer's time at the set plus the interval,
// not the interval alone. A stopped timer's time doesn't move, so its schedule is exact, and a
// running timer's is compared against the timer's time read before and after the set. The cases:
// - stopped at 2 s, a one shot event of 3 s: the schedule is exactly 5 s, the interval 3 s, and
//   the remaining time exactly 3 s. Started, the wait for the event ends over 2 s and under 4 s
//   after the start, with a 4.5 s timeout and a result of 0.
// - running at 2 s, a one shot event of 30 s: the schedule lies between the timer's time before
//   the set plus 30 s and its time after the set plus 30 s. The remaining time plus the timer's
//   time is the schedule, within 50 ms.
// - 600 ms later, the same event set again: the new schedule is 350 to 850 ms past the old one.
// - stopped, a one shot event of 4 s: the schedule is exactly the timer's time plus the interval.
//
// Each comparison is exact where the timer is stopped, and every other threshold is at least 250
// ms from the value it is checked against. Lower bounds on a delay are exact, since a delay never
// ends early.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define SEC_US 1000000ULL
#define GAP_US 600000

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    // A stopped timer at 2 s, with a one shot event 3 s away, so its schedule is 5 s
    SceUID timer = sceKernelCreateTimer("offset", EVENT_ATTR_MANUAL_RESET, NULL);
    sceKernelSetTimerTimeWide(timer, 2 * SEC_US);
    SceUInt64 interval = 3 * SEC_US;
    const int set_result = sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    TimerInfo stopped_info = { .size = sizeof(stopped_info) };
    const int info_result = sceKernelGetTimerInfo(timer, &stopped_info);
    SceKernelSysClock left = 0;
    const int remaining_result = sceKernelGetTimerEventRemainingTime(timer, &left);

    PRINT("stopped at 2 s: set a one shot event of 3 s", set_result);
    PRINT("stopped at 2 s: the info call", info_result);
    PRINT("stopped at 2 s: the schedule is exactly 5 s", stopped_info.schedule == 5 * SEC_US);
    PRINT("stopped at 2 s: the interval is 3 s", stopped_info.interval == 3 * SEC_US);
    PRINT("stopped at 2 s: the remaining time call", remaining_result);
    PRINT("stopped at 2 s: the remaining time is exactly 3 s", left == 3 * SEC_US);

    // Start, and wait for the event, which is due 3 s after the start
    const SceUInt64 before = sceKernelGetProcessTimeWide();
    const int start_result = sceKernelStartTimer(timer);
    SceUInt32 result = 0;
    SceUInt64 user_data = 0;
    SceUInt32 timeout = 4500000;
    const int wait_result = sceKernelWaitEvent(timer, EVENT_TIMER, &result, &user_data, &timeout);
    const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - before;

    PRINT("start the timer", start_result);
    PRINT("wait for the event with a timeout of 4.5 s", wait_result);
    PRINT("the wait ended over 2 s after the start", elapsed > 2 * SEC_US);
    PRINT("the wait ended under 4 s after the start", elapsed < 4 * SEC_US);
    PRINT("stop the timer after the wait", sceKernelStopTimer(timer));
    sceKernelDeleteTimer(timer);

    // A running timer at 2 s, with a one shot event 30 s away set between two reads of its time
    timer = sceKernelCreateTimer("offset", EVENT_ATTR_MANUAL_RESET, NULL);
    const int run_result = sceKernelStartTimer(timer);
    sceKernelSetTimerTimeWide(timer, 2 * SEC_US);
    const SceUInt64 time_before = sceKernelGetTimerTimeWide(timer);
    interval = 30 * SEC_US;
    const int running_set_result =
        sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    const SceUInt64 time_after = sceKernelGetTimerTimeWide(timer);
    TimerInfo running_info = { .size = sizeof(running_info) };
    const int running_info_result = sceKernelGetTimerInfo(timer, &running_info);
    const SceUInt64 time_now = sceKernelGetTimerTimeWide(timer);
    SceKernelSysClock running_left = 0;
    const int running_remaining_result = sceKernelGetTimerEventRemainingTime(timer, &running_left);
    const SceUInt64 first_schedule = running_info.schedule;
    const SceInt64 offset = (SceInt64)(running_left + time_now) - (SceInt64)first_schedule;

    // 600 ms later, the same event is set again
    sceKernelDelayThread(GAP_US);
    const int reset_result = sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    TimerInfo reset_info = { .size = sizeof(reset_info) };
    const int reset_info_result = sceKernelGetTimerInfo(timer, &reset_info);

    PRINT("running at 2 s: start the timer", run_result);
    PRINT("running at 2 s: set a one shot event of 30 s", running_set_result);
    PRINT("running at 2 s: the info call", running_info_result);
    PRINT("running at 2 s: the schedule is 30 s past the time at the set",
          running_info.schedule >= time_before + 30 * SEC_US &&
              running_info.schedule <= time_after + 30 * SEC_US);
    PRINT("running at 2 s: the remaining time call", running_remaining_result);
    PRINT("running at 2 s: the remaining time plus the timer's time is the schedule, 50 ms",
          offset > -50000 && offset < 50000);
    PRINT("600 ms later: set the same event again", reset_result);
    PRINT("600 ms later: the info call", reset_info_result);
    PRINT("600 ms later: the schedule is 350 to 850 ms past the old one",
          reset_info.schedule - first_schedule >= 350000 &&
              reset_info.schedule - first_schedule <= 850000);

    // Stopped, with the event armed, a new one shot event of 4 s
    const int stop_result = sceKernelStopTimer(timer);
    interval = 4 * SEC_US;
    const int stopped_set_result =
        sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    TimerInfo new_info = { .size = sizeof(new_info) };
    const int new_info_result = sceKernelGetTimerInfo(timer, &new_info);

    PRINT("stop the timer", stop_result);
    PRINT("stopped: set a one shot event of 4 s", stopped_set_result);
    PRINT("stopped: the info call", new_info_result);
    PRINT("stopped: the schedule is the current time plus the interval",
          new_info.schedule == new_info.currentTime + new_info.interval);

    sceKernelDeleteTimer(timer);
    test_exit();
    return 0;
}
