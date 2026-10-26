// Whether a stopped timer keeps the time left on its one shot set event when it starts again. A
// timer with a 4 s one shot set event starts, the main thread sleeps 1.5 s, stops the timer, sleeps
// 1 s, starts the timer again, and waits for its event with a 3.25 s timeout. The model
// timer_oneshot_refire covers a one shot event started again after it fired. The cases:
// - the stop and the start, each printed with its result.
// - the wait's result.
// - whether the wait returned within 3 s of its start, which it does if the event fires before the
//   timeout.
// - whether the wait returned after 2 s of its start.
// Every time is a comparison, and the thresholds are at least 250 ms from the timeout and the
// event's time.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define EVENT_US 4000000
#define STOP_AFTER_US 1500000
#define RESTART_AFTER_US 1000000
#define WAIT_US 3250000

int main(void) {
    const SceUID timer = sceKernelCreateTimer("stop start", 0, NULL);
    SceUInt64 interval = EVENT_US;
    PRINT("set a one shot set event of 4 s",
          sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0));
    PRINT("start the timer", sceKernelStartTimer(timer));
    sceKernelDelayThread(STOP_AFTER_US);
    PRINT("stop the timer after 1.5 s", sceKernelStopTimer(timer));
    sceKernelDelayThread(RESTART_AFTER_US);
    PRINT("start the timer again after 1 s", sceKernelStartTimer(timer));

    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    SceUInt32 timeout = WAIT_US;
    const SceUInt64 begin = sceKernelGetProcessTimeWide();
    const int result = sceKernelWaitEvent(timer, EVENT_TIMER, &pattern, &user_data, &timeout);
    const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - begin;
    PRINT("the wait for the event with a 3.25 s timeout", result);
    PRINT("it returned within 3 s", elapsed < 3000000);
    PRINT("it returned after 2 s", elapsed > 2000000);

    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
    test_exit();
    return 0;
}
