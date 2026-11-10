// Timer modes from the start: a one shot event against a repeating one on an auto reset timer, no
// events while the timer is stopped, and reading and setting the time of a stopped and of a running
// timer, by both the clock struct and the wide calls. Kept short, and every time is printed as a
// comparison with a wide margin.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// Generous, so a slow system doesn't fail the test
#define MAX_OVERSHOOT_US 300000

static int wait_event(SceUID timer, SceUInt32 us) {
    SceUInt32 result = 0;
    SceUInt64 user_data = 0;
    SceUInt32 timeout = us;
    return sceKernelWaitEvent(timer, EVENT_TIMER, &result, &user_data, &timeout);
}

static int poll_event(SceUID timer) {
    SceUInt32 result = 0;
    SceUInt64 user_data = 0;
    return sceKernelPollEvent(timer, EVENT_TIMER, &result, &user_data);
}

int main(void) {
    // A one shot event fires once
    SceUID timer = sceKernelCreateTimer("once", EVENT_ATTR_AUTO_RESET, NULL);
    SceUInt64 interval = 10000;
    PRINT("one shot: set the event", sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0));
    sceKernelStartTimer(timer);
    PRINT("one shot: the first wait", wait_event(timer, 200000));
    PRINT("one shot: a second wait", wait_event(timer, 100000));
    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);

    // A repeating event fires until the timer stops, and again once it starts
    timer = sceKernelCreateTimer("repeat", EVENT_ATTR_AUTO_RESET, NULL);
    PRINT("repeating: set the event", sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 1));
    sceKernelStartTimer(timer);
    PRINT("repeating: the first wait", wait_event(timer, 200000));
    PRINT("repeating: the second wait", wait_event(timer, 200000));
    PRINT("repeating: the third wait", wait_event(timer, 200000));
    sceKernelStopTimer(timer);
    // Clears an event that fired just before the stop
    poll_event(timer);
    sceKernelDelayThread(100000);
    PRINT("repeating: a poll after 100 ms stopped", poll_event(timer));
    sceKernelStartTimer(timer);
    PRINT("repeating: a wait after starting again", wait_event(timer, 200000));
    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);

    // The time of a stopped timer, by the clock struct
    timer = sceKernelCreateTimer("time", 0, NULL);
    SceKernelSysClock clock = 0xDEAD;
    PRINT("stopped: get the time", sceKernelGetTimerTime(timer, &clock));
    PRINT("stopped: a new timer is at 0", clock == 0);
    clock = 5000;
    PRINT("stopped: set the time", sceKernelSetTimerTime(timer, &clock));
    PRINT("stopped: the clock struct after the set", clock);
    PRINT("stopped: the wide time is the one set", sceKernelGetTimerTimeWide(timer) == 5000);

    // Running, it counts from the time set. Nothing is printed until the last read, since a print
    // would add to the times read.
    sceKernelStartTimer(timer);
    sceKernelDelayThread(20000);
    clock = 0;
    const int get_result = sceKernelGetTimerTime(timer, &clock);
    const SceUInt64 counted = clock;
    const SceUInt64 old = sceKernelSetTimerTimeWide(timer, 10000000);
    SceUInt64 now = sceKernelGetTimerTimeWide(timer);
    clock = 0;
    const int set_result = sceKernelSetTimerTime(timer, &clock);
    const SceUInt64 from_zero = sceKernelGetTimerTimeWide(timer);
    PRINT("running: get the time", get_result);
    PRINT("running: it counted the delay from the time set", counted >= 25000);
    PRINT("running: and not much more", counted < 25000 + MAX_OVERSHOOT_US);
    PRINT("running: setting the wide time returns the old time", old >= 25000 && old < 25000 + MAX_OVERSHOOT_US);
    PRINT("running: it counts from the time set", now >= 10000000 && now < 10000000 + MAX_OVERSHOOT_US);
    PRINT("running: set the time by the clock struct", set_result);
    PRINT("running: the clock struct after the set is the old time", clock >= 10000000 && clock < 10000000 + MAX_OVERSHOOT_US);
    PRINT("running: it counts from 0", from_zero < MAX_OVERSHOOT_US);

    // Stopped again, it keeps its time, and starting again goes on from it
    sceKernelStopTimer(timer);
    const SceUInt64 stopped = sceKernelGetTimerTimeWide(timer);
    sceKernelDelayThread(20000);
    PRINT("stopped again: it keeps its time", sceKernelGetTimerTimeWide(timer) == stopped);
    sceKernelStartTimer(timer);
    now = sceKernelGetTimerTimeWide(timer);
    PRINT("started again: it goes on from that time", now >= stopped && now < stopped + MAX_OVERSHOOT_US);
    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);

    test_exit();
    return 0;
}
