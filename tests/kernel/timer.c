#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// Generous, so a slow system doesn't fail the test
#define MAX_OVERSHOOT_US 200000

int main(void) {
    SceUInt32 result = 0;
    SceUInt64 user_data = 0;
    SceUInt64 interval = 1000;
    SceUID timer = sceKernelCreateTimer("timer", 0, NULL);

    sceKernelSetTimerEvent(timer, 0, &interval, 0);
    sceKernelStartTimer(timer);
    sceKernelDelayThread(20000);
    PRINT("the timer event fires", sceKernelPollEvent(timer, TIMER_EVENT_BIT, &result, &user_data));

    interval = 10000000;
    sceKernelSetTimerEvent(timer, 0, &interval, 0);
    PRINT("setting a new event clears the fired one", sceKernelPollEvent(timer, TIMER_EVENT_BIT, &result, &user_data));

    // Counting: start returns 1 if already started, stop returns 1 if it stopped the timer
    timer = sceKernelCreateTimer("count", 0, NULL);
    PRINT("a new timer is at 0", sceKernelGetTimerTimeWide(timer) == 0);
    PRINT("start", sceKernelStartTimer(timer));
    PRINT("start again", sceKernelStartTimer(timer));
    sceKernelDelayThread(20000);
    const SceUInt64 running = sceKernelGetTimerTimeWide(timer);
    PRINT("a running timer counts at least the time passed", running >= 20000);
    PRINT("and not much more", running < 20000 + MAX_OVERSHOOT_US);
    PRINT("stop", sceKernelStopTimer(timer));
    PRINT("stop again", sceKernelStopTimer(timer));
    const SceUInt64 stopped = sceKernelGetTimerTimeWide(timer);
    sceKernelDelayThread(10000);
    PRINT("a stopped timer doesn't count", sceKernelGetTimerTimeWide(timer) == stopped);
    PRINT("set the time returns the old time", sceKernelSetTimerTimeWide(timer, 5000) == stopped);
    PRINT("the time is the one set", sceKernelGetTimerTimeWide(timer) == 5000);

    // A repeating event fires every interval
    timer = sceKernelCreateTimer("repeat", 0, NULL);
    interval = 10000;
    sceKernelSetTimerEvent(timer, 0, &interval, 1);
    sceKernelStartTimer(timer);
    SceUInt32 timeout = 200000;
    PRINT("the first repeat", sceKernelWaitEvent(timer, TIMER_EVENT_BIT, &result, &user_data, &timeout));
    timeout = 200000;
    PRINT("the second repeat", sceKernelWaitEvent(timer, TIMER_EVENT_BIT, &result, &user_data, &timeout));
    PRINT("a timer event has no user data", user_data);

    test_exit();
    return 0;
}
