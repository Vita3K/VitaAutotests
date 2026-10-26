// Setting the time of a stopped timer, then starting it, for a one shot set event of 4 s. Timer A
// is stopped, has its time set to 1.5 s, and is started, and its event is waited for with a 3.25 s
// timeout. Timer B is stopped with its time set to 5 s, past the event's time, and is waited for
// with a 1 s timeout. The model timer_oneshot_refire covers setting the time of a timer that
// already fired. The cases:
// - for each timer, the set of its time, its start and the wait's result.
// - whether A's wait returned within 3 s of its start.
// - whether B's wait returned within 500 ms of its start.
// Each time is a comparison, and the thresholds are at least 250 ms from every outcome.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define EVENT_US 4000000

// Waits for the timer's event with a timeout, and sets elapsed to how long the wait took
static int timed_wait(SceUID timer, SceUInt32 timeout_us, SceUInt64 *elapsed) {
    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    SceUInt32 timeout = timeout_us;
    const SceUInt64 begin = sceKernelGetProcessTimeWide();
    const int result = sceKernelWaitEvent(timer, EVENT_TIMER, &pattern, &user_data, &timeout);
    *elapsed = sceKernelGetProcessTimeWide() - begin;
    return result;
}

int main(void) {
    SceUInt64 interval = EVENT_US;
    SceUInt64 elapsed = 0;

    const SceUID a = sceKernelCreateTimer("set time A", 0, NULL);
    sceKernelSetTimerEvent(a, TIMER_TYPE_SET_EVENT, &interval, 0);
    SceKernelSysClock time = 1500000;
    PRINT("A: set its time to 1.5 s while stopped", sceKernelSetTimerTime(a, &time));
    PRINT("A: start", sceKernelStartTimer(a));
    PRINT("A: the wait for its event with a 3.25 s timeout", timed_wait(a, 3250000, &elapsed));
    PRINT("A: it returned within 3 s", elapsed < 3000000);
    sceKernelStopTimer(a);
    sceKernelDeleteTimer(a);

    const SceUID b = sceKernelCreateTimer("set time B", 0, NULL);
    sceKernelSetTimerEvent(b, TIMER_TYPE_SET_EVENT, &interval, 0);
    time = 5000000;
    PRINT("B: set its time to 5 s while stopped", sceKernelSetTimerTime(b, &time));
    PRINT("B: start", sceKernelStartTimer(b));
    PRINT("B: the wait for its event with a 1 s timeout", timed_wait(b, 1000000, &elapsed));
    PRINT("B: it returned within 500 ms", elapsed < 500000);
    sceKernelStopTimer(b);
    sceKernelDeleteTimer(b);
    test_exit();
    return 0;
}
