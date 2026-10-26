// A timer's info as its one shot set event fires with no waiter, after a cancel, and after a stop.
// The timer's event is 300 ms away, the timer starts, and the main thread sleeps 600 ms with no
// waiter, so the event fired by then. The info is read after that, after a cancel with no count
// pointer, and after a stop. The model timer_info_cancel covers the info of a timer with a waiter
// and a cancel with a count pointer. The cases, for each read of the info:
// - active, and the type and repeat setting.
// - whether the schedule equals the interval, and whether it is 0.
// - whether the interval is the one set.
// - after the stop, whether the base time equals the current time.
// Times are comparisons, and the thresholds are at least 250 ms from the event's time.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define EVENT_US 300000
#define SLEEP_US 600000

// Reads the timer's info, prints it under the label when, and returns it
static TimerInfo print_info(const char *when, SceUID timer) {
    TimerInfo info = { .size = sizeof(info) };
    PRINTF(sceKernelGetTimerInfo(timer, &info), "%s: the info call", when);
    PRINTF(info.active, "%s: active", when);
    PRINTF(info.type, "%s: the type", when);
    PRINTF(info.repeat, "%s: the repeat setting", when);
    PRINTF(info.schedule == EVENT_US, "%s: the schedule is the interval", when);
    PRINTF(info.schedule == 0, "%s: the schedule is 0", when);
    PRINTF(info.interval == EVENT_US, "%s: the interval is the one set", when);
    return info;
}

int main(void) {
    const SceUID timer = sceKernelCreateTimer("info", 0, NULL);
    SceUInt64 interval = EVENT_US;
    PRINT("set a one shot set event of 300 ms",
          sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0));
    PRINT("start the timer", sceKernelStartTimer(timer));
    sceKernelDelayThread(SLEEP_US);

    print_info("after the event fired", timer);
    PRINT("cancel with no count pointer", sceKernelCancelTimer(timer, NULL));
    print_info("after the cancel", timer);
    PRINT("stop the timer", sceKernelStopTimer(timer));
    const TimerInfo stopped = print_info("after the stop", timer);
    PRINT("after the stop, the base time equals the current time",
          stopped.baseTime == stopped.currentTime);

    sceKernelDeleteTimer(timer);
    test_exit();
    return 0;
}
