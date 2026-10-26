// The info and remaining time of a repeating timer at each step. An auto reset set event of 2 s
// repeats, and the main thread waits for its fires while the timer runs. The steps, each printing
// the calls' results and comparisons:
// - before the start: the timer isn't active, its schedule is the interval, and its base time is
//   its current time.
// - the first fire, which the first wait returns at: the timer is active, its schedule moved on to
//   about 4 s, and the remaining time plus the timer's time is the schedule, within 50 ms.
// - stopped 0.5 s after the fire: the schedule is kept, the remaining time plus the timer's time is
//   the schedule exactly, and the remaining time is about 1.5 s. A poll 2 s after the stop finds no
//   event.
// - started again: the next fire comes after the time left, about 1.5 s, not a full interval. The
//   fire after that comes a full interval later.
// - cancelled: the repeats end, so a wait for 2.5 s times out.
// The schedule moves on just after a fire wakes its waiter, so the info is read once it has moved.
// Nothing is printed between the first fire and the stop, since a print would shift the time left.
// Times are comparisons, and every threshold is at least 250 ms from each outcome.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define INTERVAL 2000000
#define CLOSE_US 50000
#define STOP_AFTER_US 500000
#define STOP_FOR_US 2000000
#define FIRST_WAIT_US 4000000
#define NEXT_WAIT_US 3000000
#define LAST_WAIT_US 4000000
#define CANCELLED_WAIT_US 2500000

static SceUID timer;

// Reads the timer's info into info, and returns the call's result
static int read_info(TimerInfo *info) {
    info->size = sizeof(*info);
    return sceKernelGetTimerInfo(timer, info);
}

// Returns the timer's schedule now
static SceUInt64 schedule_now(void) {
    TimerInfo info = { .size = sizeof(info) };
    sceKernelGetTimerInfo(timer, &info);
    return info.schedule;
}

// Reads the remaining time into left, and returns the call's result
static int read_remaining(SceKernelSysClock *left) {
    *left = 0;
    return sceKernelGetTimerEventRemainingTime(timer, left);
}

// Waits for the timer bit for at most timeout_us, and returns the result and how long it took
static int wait_bit(SceUInt32 timeout_us, SceUInt64 *took) {
    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    SceUInt32 timeout = timeout_us;
    const SceUInt64 begin = sceKernelGetProcessTimeWide();
    const int result = sceKernelWaitEvent(timer, EVENT_TIMER, &pattern, &user_data, &timeout);
    *took = sceKernelGetProcessTimeWide() - begin;
    return result;
}

int main(void) {
    timer = sceKernelCreateTimer("repeat info", EVENT_ATTR_AUTO_RESET, NULL);
    SceUInt64 interval = INTERVAL;
    PRINT("set a repeating set event of 2 s",
          sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 1));

    TimerInfo info = { .size = sizeof(info) };
    PRINT("before the start: the info call", read_info(&info));
    PRINT("before the start: active", info.active);
    PRINT("before the start: the repeat setting", info.repeat);
    PRINT("before the start: the schedule is the interval", info.schedule == INTERVAL);
    PRINT("before the start: the base time equals the current time",
          info.baseTime == info.currentTime);

    PRINT("start the timer", sceKernelStartTimer(timer));
    SceUInt64 took = 0;
    const int first_wait = wait_bit(FIRST_WAIT_US, &took);
    const SceUInt64 first_took = took;

    TEST_WAIT_FOR(schedule_now() > 3750000);
    TimerInfo fire_info = { .size = sizeof(fire_info) };
    const int fire_info_result = read_info(&fire_info);
    SceKernelSysClock fire_left = 0;
    const int fire_left_result = read_remaining(&fire_left);

    // The stop comes 0.5 s after the fire, and nothing is printed until the timer is stopped, since
    // a print costs tens of milliseconds on the console and would shift the time left
    sceKernelDelayThread(STOP_AFTER_US);
    const int stop_result = sceKernelStopTimer(timer);
    const int stop_info_result = read_info(&info);
    SceKernelSysClock left = 0;
    const int stop_left_result = read_remaining(&left);

    PRINT("the first wait", first_wait);
    PRINT("the first wait returned over 1.5 s and under 2.5 s",
          first_took > 1500000 && first_took < 2500000);

    PRINT("after the first fire: the info call", fire_info_result);
    PRINT("after the first fire: active", fire_info.active);
    PRINT("after the first fire: the schedule is within 3.75 to 4.25 s",
          fire_info.schedule > 3750000 && fire_info.schedule < 4250000);
    PRINT("after the first fire: the remaining time call", fire_left_result);
    const SceInt64 off =
        (SceInt64)(fire_left + fire_info.currentTime) - (SceInt64)fire_info.schedule;
    PRINT("after the first fire: the remaining time plus the current time is the schedule",
          off > -CLOSE_US && off < CLOSE_US);
    PRINT("after the first fire: the remaining time is over 0 and at most the interval",
          fire_left > 0 && fire_left <= INTERVAL);

    PRINT("stop the timer 0.5 s after the fire", stop_result);
    PRINT("after the stop: the info call", stop_info_result);
    PRINT("after the stop: active", info.active);
    PRINT("after the stop: the schedule is unchanged", info.schedule == fire_info.schedule);
    PRINT("after the stop: the base time equals the current time",
          info.baseTime == info.currentTime);
    PRINT("after the stop: the remaining time call", stop_left_result);
    PRINT("after the stop: the remaining time plus the current time is the schedule exactly",
          (SceInt64)(left + info.currentTime) == (SceInt64)info.schedule);
    PRINT("after the stop: the remaining time is within 1.25 to 1.75 s",
          left > 1250000 && left < 1750000);

    sceKernelDelayThread(STOP_FOR_US);
    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    PRINT("a poll 2 s after the stop",
          sceKernelPollEvent(timer, EVENT_TIMER, &pattern, &user_data));

    PRINT("start the timer again", sceKernelStartTimer(timer));
    PRINT("the second wait", wait_bit(NEXT_WAIT_US, &took));
    PRINT("the second wait returned over 1 s and under 1.75 s",
          took > 1000000 && took < 1750000);
    PRINT("the third wait", wait_bit(LAST_WAIT_US, &took));
    PRINT("the third wait returned over 1.5 s and under 2.5 s",
          took > 1500000 && took < 2500000);

    TEST_WAIT_FOR(schedule_now() > 7750000);
    PRINT("after the third fire: the info call", read_info(&info));
    PRINT("after the third fire: the schedule is within 7.75 to 8.25 s",
          info.schedule > 7750000 && info.schedule < 8250000);

    PRINT("cancel the timer", sceKernelCancelTimer(timer, NULL));
    PRINT("after the cancel: the info call", read_info(&info));
    PRINT("after the cancel: active", info.active);
    PRINT("after the cancel: the repeat setting", info.repeat);
    PRINT("after the cancel: the interval is the one set", info.interval == INTERVAL);
    PRINT("after the cancel: the schedule is 0", info.schedule == 0);
    PRINT("after the cancel: the schedule is within 7.75 to 8.25 s",
          info.schedule > 7750000 && info.schedule < 8250000);
    PRINT("the wait 2.5 s after the cancel", wait_bit(CANCELLED_WAIT_US, &took));

    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
    test_exit();
    return 0;
}
