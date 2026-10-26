// The repeat argument of sceKernelSetTimerEvent on a timer that is stopped. Each repeat value gets
// a fresh timer, which is never started, so its event never fires. The model timer_info_cancel
// covers the info of a timer with a set event. The cases are repeat 0, 1, 2 and -1. For each one
// the set's result is printed, then the repeat field of the timer's info.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define INTERVAL_US 1000000

int main(void) {
    static const int repeats[] = { 0, 1, 2, -1 };
    for (unsigned i = 0; i < sizeof(repeats) / sizeof(repeats[0]); i++) {
        const SceUID timer = sceKernelCreateTimer("repeat", 0, NULL);
        SceUInt64 interval = INTERVAL_US;
        PRINTF(sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, repeats[i]),
               "repeat %d: the set", repeats[i]);
        TimerInfo info = { .size = sizeof(info) };
        PRINTF(sceKernelGetTimerInfo(timer, &info), "repeat %d: the info call", repeats[i]);
        PRINTF(info.repeat, "repeat %d: the repeat field", repeats[i]);
        sceKernelDeleteTimer(timer);
    }
    test_exit();
    return 0;
}
