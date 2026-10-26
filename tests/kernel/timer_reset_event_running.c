// Setting a new event on a timer that is already running. A manual reset timer starts with a one
// shot set event of 2 s. A waiter starts with it and waits for the timer bit 0x8000 with a timeout
// of 1 s. At 300 ms the main thread sets a new one shot set event of 400 ms on the running timer.
// Once the waiter has ended, the main thread clears every bit of the event at 1 s, and polls for
// the timer bit at 2.5 s. The test prints the result of the set, the waiter's result, whether it
// returned within 1 s, the result of the clear, and the result of the poll.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define WAITER_TIMEOUT_US 1000000

static SceUID timer;
static int wait_result;
static SceUInt64 wait_elapsed;

// Waits for the timer bit with a timeout of 1 s, and keeps the result and how long the wait took
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = WAITER_TIMEOUT_US;
    SceUInt32 bits = 0;
    SceUInt64 user_data = 0;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    wait_result = sceKernelWaitEvent(timer, EVENT_TIMER, &bits, &user_data, &timeout);
    wait_elapsed = sceKernelGetProcessTimeWide() - start;
    return 0;
}

// Sleeps until the process time reaches until, in microseconds
static void sleep_until(SceUInt64 until) {
    while (sceKernelGetProcessTimeWide() < until)
        sceKernelDelayThread(1000);
}

int main(void) {
    SceUInt64 interval = 2000000;
    SceUInt32 bits = 0;
    SceUInt64 user_data = 0;

    timer = sceKernelCreateTimer("reset", EVENT_ATTR_MANUAL_RESET, NULL);
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    sceKernelStartTimer(timer);

    const SceUID thread = test_start_thread(waiter);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_EVENT, timer));
    sleep_until(start + 300000);
    interval = 400000;
    PRINT("set a new one shot event of 400 ms",
          sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0));
    test_join_thread(thread);
    PRINT("the waiter's result", wait_result);
    PRINT("it returned within 1 s", wait_elapsed < 1000000);

    sleep_until(start + 1000000);
    PRINT("clear the timer bit", sceKernelClearEvent(timer, 0));
    sleep_until(start + 2500000);
    PRINT("the poll for the timer bit after the clear",
          sceKernelPollEvent(timer, EVENT_TIMER, &bits, &user_data));

    sceKernelDeleteTimer(timer);
    test_exit();
    return 0;
}
