// What sceKernelCancelEventWithSetPattern does to a timer that has a waiter. The timer has a 30 s
// one shot set event and is started, then a waiter waits for the timer bit 0x8000 with a timeout of
// 5 s. Once the waiter is queued, the main thread cancels the timer with the set pattern
// 0x01000002, user data 0x77 and a count pointer. The test prints the cancel's result and the count
// it wrote, the waiter's result, result pattern and user data, whether the pattern and the user
// data after the cancel are the given ones, the active flag of the timer, and the result of a poll
// for 0x8000.
// The pattern 0x01000002 has the system bit 0x2, so that cancel is refused before anything runs and
// the waiter times out. A second case, whose lines end with ", user bit only", repeats it with the
// set pattern 0x01000000, which has no system bit.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define SET_PATTERN 0x01000002
#define USER_SET_PATTERN 0x01000000
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

// Runs the case with the given set pattern. The labels of every line end with suffix.
static void run_case(SceUInt32 set_pattern, const char *suffix) {
    SceUInt64 interval = 30000000;
    SceUInt32 count = NOT_DONE;
    SceUInt32 bits = NOT_DONE;
    SceUInt64 user_data = NOT_DONE;
    wait_result = NOT_DONE;
    wait_bits = NOT_DONE;
    wait_user_data = NOT_DONE;

    timer = sceKernelCreateTimer("cancel", 0, NULL);
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    sceKernelStartTimer(timer);

    const SceUID thread = test_start_thread(waiter);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_EVENT, timer));
    PRINTF(sceKernelCancelEventWithSetPattern(timer, set_pattern, 0x77, &count), "cancel and set%s", suffix);
    PRINTF(count, "the waiting count%s", suffix);
    test_join_thread(thread);
    PRINTF(wait_result, "the waiter's result%s", suffix);
    PRINTF(wait_bits, "the waiter's result pattern%s", suffix);
    PRINTF((SceUInt32)wait_user_data, "the waiter's user data, low word%s", suffix);
    PRINTF((SceUInt32)(wait_user_data >> 32), "the waiter's user data, high word%s", suffix);

    EventInfo info = { .size = sizeof(info) };
    sceKernelGetEventInfo(timer, &info);
    PRINTF(info.eventPattern == set_pattern, "the pattern after is the set pattern%s", suffix);
    PRINTF(info.userData == 0x77, "the user data after is the given one%s", suffix);

    TimerInfo timer_info = { .size = sizeof(timer_info) };
    sceKernelGetTimerInfo(timer, &timer_info);
    PRINTF(timer_info.active, "the timer's active flag%s", suffix);

    PRINTF(sceKernelPollEvent(timer, EVENT_TIMER, &bits, &user_data), "a poll for 0x8000%s", suffix);

    sceKernelDeleteTimer(timer);
}

int main(void) {
    // The set pattern has the system bit 0x2, so the cancel is refused
    run_case(SET_PATTERN, "");
    // The same case with a set pattern that only has a user bit
    run_case(USER_SET_PATTERN, ", user bit only");
    test_exit();
    return 0;
}
