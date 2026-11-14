// How often a repeating timer event fires when its interval wraps the deadline. Each case gets a
// fresh timer with a callback registered to it, sets a repeating set event of the case's interval,
// and starts the timer. The callback never runs, since no wait here runs callbacks, so its notify
// count only grows with the fires. The test reads the count after 20 ms, after 40 ms, then stops
// the timer and reads it again after 20 more ms. The cases are:
// - interval 10 ms, the reference, which fires about every 10 ms
// - interval 0xFFFFFFFFFFFFFFFF, whose next deadline after each fire is one microsecond before the
//   last one if the sum wraps in 64 bits
// - interval 0x8000000000000000, whose first deadline doesn't wrap, the reference for a huge
//   interval
// The counts are printed as ranges, so the output stays the same from run to run. The info's
// schedule and active fields are read while the timer runs, and every call's result is printed once
// the samples are taken.
//
// On a PS Vita TV on firmware 3.65 the 10 ms reference fires 2 to 10 times by 20 ms and again by
// 40 ms. The interval of all ones keeps firing: 11 to 1000 fires by 20 ms and more by 40 ms, and
// its schedule reads below the interval, so each next deadline wraps. Both stop firing with the
// timer. The interval 0x8000000000000000 never fires.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static int callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    return 0;
}

// Prints which range count falls in
static void print_count(const char *name, const char *when, int count) {
    PRINTF(count == 0, "%s: %s: no fire", name, when);
    PRINTF(count == 1, "%s: %s: one fire", name, when);
    PRINTF(count >= 2 && count <= 10, "%s: %s: 2 to 10 fires", name, when);
    PRINTF(count > 10 && count <= 1000, "%s: %s: 11 to 1000 fires", name, when);
    PRINTF(count > 1000, "%s: %s: more than 1000 fires", name, when);
}

static void run(const char *name, SceUInt64 interval) {
    const SceUID timer = sceKernelCreateTimer("wrap", 0, NULL);
    const SceUID cb = sceKernelCreateCallback("wrap", 0, callback, NULL);
    const int register_result = sceKernelRegisterCallbackToEvent(timer, cb);
    SceUInt64 value = interval;
    const int set_result = sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &value, 1);
    const int start_result = sceKernelStartTimer(timer);

    sceKernelDelayThread(20000);
    const int at_20 = sceKernelGetCallbackCount(cb);
    sceKernelDelayThread(20000);
    const int at_40 = sceKernelGetCallbackCount(cb);
    TimerInfo info = { .size = sizeof(info) };
    const int info_result = sceKernelGetTimerInfo(timer, &info);
    const int stop_result = sceKernelStopTimer(timer);
    const int at_stop = sceKernelGetCallbackCount(cb);
    sceKernelDelayThread(20000);
    const int after_stop = sceKernelGetCallbackCount(cb);

    PRINTF(register_result, "%s: register the callback", name);
    PRINTF(set_result, "%s: set the repeating event", name);
    PRINTF(start_result, "%s: start the timer", name);
    print_count(name, "after 20 ms", at_20);
    print_count(name, "after 40 ms", at_40);
    PRINTF(at_40 > at_20, "%s: it fired between 20 ms and 40 ms", name);
    PRINTF(info_result, "%s: the info call while running", name);
    PRINTF(info.active, "%s: active while running", name);
    PRINTF(info.schedule == interval, "%s: the schedule is the interval", name);
    PRINTF(info.schedule < interval, "%s: the schedule is below the interval", name);
    PRINTF(stop_result, "%s: stop the timer", name);
    PRINTF(after_stop == at_stop, "%s: no fire after the stop", name);
    PRINTF(sceKernelUnregisterCallbackFromEvent(timer, cb), "%s: unregister the callback", name);
    PRINTF(sceKernelDeleteCallback(cb), "%s: delete the callback", name);
    PRINTF(sceKernelDeleteTimer(timer), "%s: delete the timer", name);
}

int main(void) {
    run("interval 10 ms", 10000);
    run("interval 0xFFFFFFFFFFFFFFFF", 0xFFFFFFFFFFFFFFFFULL);
    run("interval 0x8000000000000000", 0x8000000000000000ULL);
    test_exit();
    return 0;
}
