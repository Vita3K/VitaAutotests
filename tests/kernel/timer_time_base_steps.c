// The base and the time of a timer through each step of a start, a stop, a start again, a set of
// the time while running, and several stop and start cycles, and that the time doesn't count while
// the timer is stopped. P is the process time, read before and after each call where it matters.
// The cases:
// - the first start: the base plus the time lies between the two P reads, and the struct time read
//   is not below the wide time read before it
// - 600 ms running, then a stop: the count is at least 600 ms and under 850 ms, and the base
//   reads 0
// - 1 s stopped, then a start: the base plus the count from before the stop lies between the P
//   reads around the start, the process time before the start is at least 350 ms past the new
//   base, the new base minus the first base is within 250 ms of the time between the two starts
//   less the count, and right after the start the time is at least the count and under the count
//   plus 250 ms
// - a set of the time to 10 s while running: the base plus 10 s lies between the P reads around
//   the set. A stop then reads base 0 and a count of 10 s to 10.25 s, and a start after it puts
//   the base plus the time between the P reads again
// - a fresh timer through three cycles of 400 ms running and 600 ms stopped: after each start the
//   base plus the time lies between the P reads, and after the last stop the total is 1.2 s to
//   1.45 s (a timer that counted its stopped time would read 3 s)
//
// The first steps are printed only after the restart, since a print costs tens of milliseconds on
// the console and would shift the timing of the restart. Times are only compared. Lower bounds on
// a delay are exact, since a delay never ends early, and every upper bound is at least 250 ms away
// from the value it is checked against.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define SEC_US 1000000ULL
#define CYCLES 3

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    SceUID timer = sceKernelCreateTimer("steps", 0, NULL);

    // The first start
    const SceUInt64 start_before = sceKernelGetProcessTimeWide();
    const int start_result = sceKernelStartTimer(timer);
    const SceUInt64 first_base = sceKernelGetTimerBaseWide(timer);
    const SceUInt64 first_time = sceKernelGetTimerTimeWide(timer);
    const SceUInt64 start_after = sceKernelGetProcessTimeWide();
    SceKernelSysClock struct_time = 0;
    const int struct_result = sceKernelGetTimerTime(timer, &struct_time);
    const SceUInt64 first_sum = first_base + first_time;

    // Run 600 ms, then stop
    sceKernelDelayThread(600000);
    const int stop_result = sceKernelStopTimer(timer);
    const SceUInt64 count = sceKernelGetTimerTimeWide(timer);
    const SceUInt64 stopped_base = sceKernelGetTimerBaseWide(timer);

    // Stay stopped 1 s, then start again
    sceKernelDelayThread(1000000);
    const SceUInt64 restart_before = sceKernelGetProcessTimeWide();
    const int restart_result = sceKernelStartTimer(timer);
    const SceUInt64 restart_after = sceKernelGetProcessTimeWide();
    const SceUInt64 second_base = sceKernelGetTimerBaseWide(timer);
    const SceUInt64 restart_time = sceKernelGetTimerTimeWide(timer);
    // The base moves by the time between the two starts, less the count the timer kept
    const SceInt64 shift = (SceInt64)(second_base - first_base);
    const SceInt64 between = (SceInt64)(restart_before - start_before) - (SceInt64)count;

    PRINT("start: the result", start_result);
    PRINT("start: the base plus the time lies between the process time reads around it",
          first_sum >= start_before && first_sum <= start_after);
    PRINT("start: the struct time call: the result", struct_result);
    PRINT("start: the struct time is not below the wide time read before it",
          struct_time >= first_time);
    PRINT("run 600 ms, stop: the result", stop_result);
    PRINT("run 600 ms, stop: the count is at least 600 ms", count >= 600000);
    PRINT("run 600 ms, stop: the count is under 850 ms", count < 850000);
    PRINT("stopped: the base reads 0", stopped_base == 0);

    PRINT("start again after 1 s stopped: the result", restart_result);
    PRINT("start again: the base plus the old count lies between the process time reads",
          second_base + count >= restart_before && second_base + count <= restart_after);
    PRINT("start again: the process time before it is at least 350 ms past the new base",
          restart_before - second_base >= 350000);
    PRINT("start again: the new base is the time between the starts minus the old count",
          shift >= between - 250000 && shift <= between + 250000);
    PRINT("start again: right after it, the time is at least the old count",
          restart_time >= count);
    PRINT("start again: right after it, the time is under the old count plus 250 ms",
          restart_time < count + 250000);

    // Set the time to 10 s while running, then stop, then start again
    const SceUInt64 set_before = sceKernelGetProcessTimeWide();
    sceKernelSetTimerTimeWide(timer, 10 * SEC_US);
    const SceUInt64 set_after = sceKernelGetProcessTimeWide();
    const SceUInt64 set_base = sceKernelGetTimerBaseWide(timer);
    const int stop2_result = sceKernelStopTimer(timer);
    const SceUInt64 set_count = sceKernelGetTimerTimeWide(timer);
    const SceUInt64 set_stopped_base = sceKernelGetTimerBaseWide(timer);

    PRINT("set to 10 s while running: the base plus 10 s lies between the process time reads",
          set_base + 10 * SEC_US >= set_before && set_base + 10 * SEC_US <= set_after);
    PRINT("stop after the set: the result", stop2_result);
    PRINT("stopped after the set: the base reads 0", set_stopped_base == 0);
    PRINT("stopped after the set: the count is 10 s to 10.25 s",
          set_count >= 10 * SEC_US && set_count < 10250000);

    const SceUInt64 again_before = sceKernelGetProcessTimeWide();
    const int again_result = sceKernelStartTimer(timer);
    const SceUInt64 again_base = sceKernelGetTimerBaseWide(timer);
    const SceUInt64 again_time = sceKernelGetTimerTimeWide(timer);
    const SceUInt64 again_after = sceKernelGetProcessTimeWide();

    PRINT("start after the set: the result", again_result);
    PRINT("start after the set: the base plus the time lies between the process time reads",
          again_base + again_time >= again_before && again_base + again_time <= again_after);

    PRINT("stop after the restart: the result", sceKernelStopTimer(timer));
    sceKernelDeleteTimer(timer);

    // Three cycles on a fresh timer, each 400 ms running and 600 ms stopped
    timer = sceKernelCreateTimer("cycles", 0, NULL);
    for (int i = 0; i < CYCLES; i++) {
        const SceUInt64 before = sceKernelGetProcessTimeWide();
        const int result = sceKernelStartTimer(timer);
        const SceUInt64 base = sceKernelGetTimerBaseWide(timer);
        const SceUInt64 timer_time = sceKernelGetTimerTimeWide(timer);
        const SceUInt64 after = sceKernelGetProcessTimeWide();
        sceKernelDelayThread(400000);
        const int stopped = sceKernelStopTimer(timer);
        sceKernelDelayThread(600000);

        PRINTF(result, "cycle %d: start the timer", i);
        PRINTF(base + timer_time >= before && base + timer_time <= after,
               "cycle %d: the base plus the time lies between the process time reads", i);
        PRINTF(stopped, "cycle %d: stop the timer", i);
    }
    const SceUInt64 total = sceKernelGetTimerTimeWide(timer);
    PRINT("three cycles: the total after the last stop is 1.2 s to 1.45 s",
          total >= 1200000 && total < 1450000);

    sceKernelDeleteTimer(timer);
    test_exit();
    return 0;
}
