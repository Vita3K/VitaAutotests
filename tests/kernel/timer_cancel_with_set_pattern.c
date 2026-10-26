// What sceKernelCancelEventWithSetPattern does to a timer that has a waiter. The timer has a 30 s
// one shot set event and is started, then a waiter waits for the timer bit 0x8000 with a timeout of
// 5 s. Once the waiter is queued, the main thread cancels the timer with the set pattern
// 0x01000002, user data 0x77 and a count pointer. The test prints the cancel's result and the count
// it wrote, the waiter's result, result pattern and user data, whether the pattern and the user
// data after the cancel are the given ones, the active flag of the timer, and the result of a poll
// for 0x8000.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define SET_PATTERN 0x01000002
#define WAITER_TIMEOUT_US 5000000

static SceUID timer;
static int wait_result;
static SceUInt32 wait_bits;
static SceUInt64 wait_user_data;

// Waits for the timer bit with a timeout of 5 s, and keeps what the wait returned
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = WAITER_TIMEOUT_US;
    SceUInt32 bits = NOT_DONE;
    SceUInt64 user_data = NOT_DONE;
    wait_result = sceKernelWaitEvent(timer, EVENT_TIMER, &bits, &user_data, &timeout);
    wait_bits = bits;
    wait_user_data = user_data;
    return 0;
}

int main(void) {
    SceUInt64 interval = 30000000;
    SceUInt32 count = NOT_DONE;
    SceUInt32 bits = NOT_DONE;
    SceUInt64 user_data = NOT_DONE;

    timer = sceKernelCreateTimer("cancel", 0, NULL);
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    sceKernelStartTimer(timer);

    const SceUID thread = test_start_thread(waiter);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_EVENT, timer));
    PRINT("cancel and set", sceKernelCancelEventWithSetPattern(timer, SET_PATTERN, 0x77, &count));
    PRINT("the waiting count", count);
    test_join_thread(thread);
    PRINT("the waiter's result", wait_result);
    PRINT("the waiter's result pattern", wait_bits);
    PRINT("the waiter's user data, low word", (SceUInt32)wait_user_data);
    PRINT("the waiter's user data, high word", (SceUInt32)(wait_user_data >> 32));

    EventInfo info = { .size = sizeof(info) };
    sceKernelGetEventInfo(timer, &info);
    PRINT("the pattern after is the set pattern", info.eventPattern == SET_PATTERN);
    PRINT("the user data after is the given one", info.userData == 0x77);

    TimerInfo timer_info = { .size = sizeof(timer_info) };
    sceKernelGetTimerInfo(timer, &timer_info);
    PRINT("the timer's active flag", timer_info.active);

    PRINT("a poll for 0x8000", sceKernelPollEvent(timer, EVENT_TIMER, &bits, &user_data));

    sceKernelDeleteTimer(timer);
    test_exit();
    return 0;
}
