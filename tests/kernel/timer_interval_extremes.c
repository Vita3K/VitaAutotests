// Very large intervals of a one shot set event, on a fresh timer for each: 0x7FFFFFFF,
// 0x80000000, 0xFFFFFFFF and 0x100000000 microseconds. A 32 bit truncation of the interval would
// fire these at once or never, so each one is checked as follows. While the timer is stopped, the
// set result is printed, then whether the info call's interval and schedule are the interval, and
// whether the remaining time is. Then the timer starts, a wait with a 500 ms timeout is printed,
// and the info shows whether the timer is still active. The timer is stopped and deleted after
// that. The intervals are in the order listed. The times are never printed, only the results and
// the comparisons.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define WAIT_US 500000

// Sets a one shot event of interval on a fresh timer, and prints the checks described above under
// the name of the interval
static void check_interval(const char *name, SceUInt64 interval) {
    const SceUID timer = sceKernelCreateTimer("interval", 0, NULL);
    SceUInt64 value = interval;
    PRINTF(sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &value, 0),
           "%s: set the event", name);

    TimerInfo info = { .size = sizeof(info) };
    PRINTF(sceKernelGetTimerInfo(timer, &info), "%s: the info call", name);
    PRINTF(info.interval == interval, "%s: the info interval is the interval", name);
    PRINTF(info.schedule == interval, "%s: the info schedule is the interval", name);
    SceKernelSysClock left = 0;
    PRINTF(sceKernelGetTimerEventRemainingTime(timer, &left), "%s: the remaining time", name);
    PRINTF(left == interval, "%s: the remaining time is the interval", name);

    PRINTF(sceKernelStartTimer(timer), "%s: start the timer", name);
    SceUInt32 result = 0;
    SceUInt64 user_data = 0;
    SceUInt32 timeout = WAIT_US;
    PRINTF(sceKernelWaitEvent(timer, EVENT_TIMER, &result, &user_data, &timeout),
           "%s: wait 500 ms for the event", name);
    TimerInfo running = { .size = sizeof(running) };
    PRINTF(sceKernelGetTimerInfo(timer, &running), "%s: the info call while running", name);
    PRINTF(running.active, "%s: active after the wait", name);

    PRINTF(sceKernelStopTimer(timer), "%s: stop the timer", name);
    PRINTF(sceKernelDeleteTimer(timer), "%s: delete the timer", name);
}

int main(void) {
    check_interval("interval 0x7FFFFFFF", 0x7FFFFFFFULL);
    check_interval("interval 0x80000000", 0x80000000ULL);
    check_interval("interval 0xFFFFFFFF", 0xFFFFFFFFULL);
    check_interval("interval 0x100000000", 0x100000000ULL);
    test_exit();
    return 0;
}
