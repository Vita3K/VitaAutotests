// Timer events in less common cases. Times are printed as comparisons so they don't depend on
// timing.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    SceUInt32 result;
    SceUInt64 user_data;
    SceUInt64 interval;
    SceUInt32 timeout;

    // Whether setting the time past a set event reports it at once
    SceUID timer = sceKernelCreateTimer("past", 0, NULL);
    interval = 1000000;
    sceKernelSetTimerEvent(timer, 0, &interval, 0);
    sceKernelStartTimer(timer);
    sceKernelSetTimerTimeWide(timer, 2000000);
    PRINT("an event set before the time jumps past it", sceKernelPollEvent(timer, TIMER_EVENT_BIT, &result, &user_data));
    PRINT("the result pattern", result);

    // A pulse event is only seen by threads waiting when it fires
    timer = sceKernelCreateTimer("pulse", 0, NULL);
    interval = 5000;
    PRINT("set a pulse event", sceKernelSetTimerEvent(timer, 1, &interval, 0));
    sceKernelStartTimer(timer);
    sceKernelDelayThread(20000);
    PRINT("poll after a pulse event fired", sceKernelPollEvent(timer, TIMER_EVENT_BIT, &result, &user_data));

    // Stopping a timer before its event
    timer = sceKernelCreateTimer("stop", 0, NULL);
    interval = 10000;
    sceKernelSetTimerEvent(timer, 0, &interval, 0);
    sceKernelStartTimer(timer);
    sceKernelStopTimer(timer);
    sceKernelDelayThread(30000);
    PRINT("an event of a timer stopped before it", sceKernelPollEvent(timer, TIMER_EVENT_BIT, &result, &user_data));
    sceKernelStartTimer(timer);
    timeout = 200000;
    PRINT("the event after starting again", sceKernelWaitEvent(timer, TIMER_EVENT_BIT, &result, &user_data, &timeout));

    // The base time and the current time
    timer = sceKernelCreateTimer("base", 0, NULL);
    PRINT("the base of a timer never started", sceKernelGetTimerBaseWide(timer) == 0);
    const SceUInt64 before = sceKernelGetProcessTimeWide();
    sceKernelStartTimer(timer);
    const SceUInt64 base = sceKernelGetTimerBaseWide(timer);
    PRINT("the base of a started timer is the process time it started at", base >= before && base <= sceKernelGetProcessTimeWide());

    // Canceling
    SceUInt32 waiting = 0xDEAD;
    PRINT("cancel with no waiters", sceKernelCancelTimer(timer, &waiting));
    PRINT("it counts no waiters", waiting);
    PRINT("cancel an unknown timer", sceKernelCancelTimer(0x12345, &waiting));

    // How many waiters an auto reset timer event wakes
    timer = sceKernelCreateTimer("auto", EVENT_ATTR_AUTO_RESET, NULL);
    interval = 5000;
    sceKernelSetTimerEvent(timer, 0, &interval, 0);
    sceKernelStartTimer(timer);
    sceKernelDelayThread(20000);
    PRINT("poll an auto reset timer event", sceKernelPollEvent(timer, TIMER_EVENT_BIT, &result, &user_data));
    PRINT("poll it again", sceKernelPollEvent(timer, TIMER_EVENT_BIT, &result, &user_data));

    test_exit();
    return 0;
}
