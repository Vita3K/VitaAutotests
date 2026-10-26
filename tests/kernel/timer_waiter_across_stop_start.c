// Whether a thread queued on a timer keeps its wait while the timer is stopped, and gets the event
// after the time left once the timer starts again. A manual reset timer gets a one shot set event
// of 3 s while it is stopped, and two waiters queue on it before it starts: T with a timeout of
// 8 s, and U with a timeout of 2.25 s. The main thread starts the timer at 1 s, stops it at 2 s,
// and starts it again at 3.5 s. The cases:
// - the waiters the timer counts before the start and at the stop, and the time left at the stop,
//   which is about 2 s since the timer counted 1 s while it ran.
// - U's wait, which ends with a timeout while the timer is stopped, after its own 2.25 s.
// - at 3.5 s, after U's timeout, T is still in its event wait and the timer counts 1 waiter.
// - T's wait after the restart, which ends with the event 2 s after the restart: not at once, not
//   after a full interval of 3 s, and not at its own timeout of 8 s.
// Every time is a comparison, and each threshold is at least 250 ms from every outcome it tells
// apart.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define EVENT_US 3000000
#define START_US 1000000
#define STOP_US 2000000
#define RESTART_US 3500000
#define SHORT_TIMEOUT_US 2250000
#define LONG_TIMEOUT_US 8000000
#define NOT_DONE 0xDEAD

typedef struct Waiter {
    SceUInt32 timeout_us;
    volatile int result;
    volatile SceUInt64 begin;
    volatile SceUInt64 end;
} Waiter;

static SceUID timer;
static Waiter long_waiter, short_waiter;

// Waits for the timer's bit with the timeout of the Waiter its argument points to, and records the
// result and the times the wait began and ended
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    Waiter *const w = *(Waiter **)argp;
    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    SceUInt32 timeout = w->timeout_us;
    w->begin = sceKernelGetProcessTimeWide();
    w->result = sceKernelWaitEvent(timer, EVENT_TIMER, &pattern, &user_data, &timeout);
    w->end = sceKernelGetProcessTimeWide();
    return 0;
}

// Starts a waiter on core 0 and returns once it waits on the timer
static SceUID start_waiter(Waiter *w, SceUInt32 timeout_us) {
    w->timeout_us = timeout_us;
    w->result = NOT_DONE;
    w->begin = 0;
    w->end = 0;
    const SceUID thread = sceKernelCreateThread(
        "waiter", waiter_entry, 0x80, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, sizeof(w), &w);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_EVENT, timer));
    return thread;
}

// Sleeps until the process time reaches until, in microseconds
static void sleep_until(SceUInt64 until) {
    while (sceKernelGetProcessTimeWide() < until)
        sceKernelDelayThread(1000);
}

// Prints the result of the info call, and how many waiters the timer counts
static void print_waiters(const char *when) {
    TimerInfo info = { .size = sizeof(info) };
    PRINTF(sceKernelGetTimerInfo(timer, &info), "%s: the info call", when);
    PRINTF(info.numWaitThreads, "%s: the waiters the timer counts", when);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    timer = sceKernelCreateTimer("across", EVENT_ATTR_MANUAL_RESET, NULL);
    SceUInt64 interval = EVENT_US;
    PRINT("set a one shot set event of 3 s",
          sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0));

    const SceUInt64 origin = sceKernelGetProcessTimeWide();
    const SceUID long_thread = start_waiter(&long_waiter, LONG_TIMEOUT_US);
    const SceUID short_thread = start_waiter(&short_waiter, SHORT_TIMEOUT_US);
    print_waiters("before the start");

    sleep_until(origin + START_US);
    PRINT("start the timer at 1 s", sceKernelStartTimer(timer));
    sleep_until(origin + STOP_US);
    PRINT("stop the timer at 2 s", sceKernelStopTimer(timer));
    print_waiters("after the stop at 2 s");
    SceKernelSysClock left = 0;
    PRINT("the remaining time at 2 s", sceKernelGetTimerEventRemainingTime(timer, &left));
    PRINT("the remaining time is within 1.75 to 2.25 s", left > 1750000 && left < 2250000);

    test_join_thread(short_thread);
    PRINT("U's wait with a 2.25 s timeout", short_waiter.result);
    const SceUInt64 short_elapsed = short_waiter.end - short_waiter.begin;
    PRINT("U's wait took at least its timeout", short_elapsed >= SHORT_TIMEOUT_US);
    PRINT("U's wait ended under 2.75 s", short_elapsed < 2750000);

    sleep_until(origin + RESTART_US);
    PRINT("at 3.5 s T is still in its event wait",
          test_in_wait(long_thread, WAIT_TYPE_EVENT, timer));
    print_waiters("at 3.5 s");
    const SceUInt64 restart = sceKernelGetProcessTimeWide();
    PRINT("start the timer again at 3.5 s", sceKernelStartTimer(timer));
    test_join_thread(long_thread);
    PRINT("T's wait", long_waiter.result);
    const SceUInt64 since_restart = long_waiter.end - restart;
    PRINT("T returned over 1.5 s after the restart", since_restart > 1500000);
    PRINT("T returned under 2.5 s after the restart", since_restart < 2500000);

    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
    test_exit();
    return 0;
}
