#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID event;
static int wait_result[2];
static SceUInt32 wait_pattern[2];
static SceUInt64 wait_user_data[2];

// Waits for EVENT_BIT_A into slot i of the results
static int wait_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    wait_result[i] = sceKernelWaitEvent(event, EVENT_BIT_A, &wait_pattern[i], &wait_user_data[i], &timeout);
    return 0;
}

// Starts two waiters for EVENT_BIT_A on a new event with the given attribute
static void start_two_waiters(SceUInt32 attr, SceUID threads[2]) {
    event = sceKernelCreateSimpleEvent("event", attr, 0, NULL);
    for (int i = 0; i < 2; i++) {
        wait_result[i] = -1;
        wait_pattern[i] = 0;
        wait_user_data[i] = 0;
        threads[i] = test_start_thread_arg(wait_thread, i);
        test_wait_until_waiting(threads[i]);
    }
}

static int woken(void) {
    return (wait_result[0] == 0) + (wait_result[1] == 0);
}

int main(void) {
    SceUInt32 result = 0xDEAD;
    SceUInt32 timeout = 1000;
    SceUInt64 user_data = 0;
    SceUInt32 pattern = 0;
    SceUID threads[2];
    event = sceKernelCreateSimpleEvent("event", 0, 0, NULL);

    PRINT("poll on a clear event fails", sceKernelPollEvent(event, EVENT_BIT_A, &result, &user_data));
    PRINT("the result after a failed poll", result);
    PRINT("wait on a clear event times out", sceKernelWaitEvent(event, EVENT_BIT_A, &result, &user_data, &timeout));
    PRINT("the result after a timed out wait", result);

    PRINT("set", sceKernelSetEvent(event, EVENT_BIT_B, 0));
    PRINT("pulse", sceKernelPulseEvent(event, EVENT_BIT_B, 0));
    sceKernelGetEventPattern(event, &pattern);
    PRINT("pulse clears a bit that was set", pattern);

    // A wait needs any one of its bits, and gets the bits and user data that were set
    sceKernelSetEvent(event, EVENT_BIT_B | EVENT_BIT_C, 0x1234);
    PRINT("poll for one of two bits", sceKernelPollEvent(event, EVENT_BIT_A | EVENT_BIT_B, &result, &user_data));
    PRINT("the poll gets the bits", result);
    PRINT("the poll gets the user data", user_data == 0x1234);

    // Manual reset: setting wakes every waiter, and the bit stays set
    start_two_waiters(EVENT_ATTR_MANUAL_RESET, threads);
    sceKernelSetEvent(event, EVENT_BIT_A, 0x42);
    test_join_thread(threads[0]);
    test_join_thread(threads[1]);
    PRINT("manual reset set wakes both waiters", woken());
    PRINT("the waiter gets the bit", wait_pattern[0]);
    PRINT("the waiter gets the user data", wait_user_data[0] == 0x42);
    sceKernelGetEventPattern(event, &pattern);
    PRINT("the bit stays set", pattern);

    // Auto reset: each bit wakes one waiter, and is cleared by it
    start_two_waiters(EVENT_ATTR_AUTO_RESET, threads);
    sceKernelSetEvent(event, EVENT_BIT_A, 0);
    test_let_threads_wait();
    PRINT("auto reset set wakes one waiter", woken());
    sceKernelGetEventPattern(event, &pattern);
    PRINT("the bit that woke it is cleared", pattern);
    sceKernelSetEvent(event, EVENT_BIT_A, 0);
    test_join_thread(threads[0]);
    test_join_thread(threads[1]);
    PRINT("setting it again wakes the other", woken());
    sceKernelSetEvent(event, EVENT_BIT_A, 0);
    sceKernelGetEventPattern(event, &pattern);
    PRINT("a bit that woke nobody stays set", pattern);

    // Pulse wakes every waiter of a manual reset event, and leaves the bit clear
    start_two_waiters(EVENT_ATTR_MANUAL_RESET, threads);
    sceKernelPulseEvent(event, EVENT_BIT_A, 0);
    test_join_thread(threads[0]);
    test_join_thread(threads[1]);
    PRINT("pulse wakes both waiters", woken());
    sceKernelGetEventPattern(event, &pattern);
    PRINT("pulse leaves the bit clear", pattern);

    test_exit();
    return 0;
}
