// Waits for pattern 0 on a timer and on a simple event, in the ways simple_event_edges leaves out.
// simple_event_edges covers a wait for pattern 0 on a simple event that ends by its timeout, a cancel
// or a delete, and a wait that a set or a pulse doesn't end. The cases here:
// - a wait for pattern 0 on a timer, with a timeout of 600 ms, which ends by its timeout. It prints
//   the result, whether it took at least 300 ms, and the out values it wrote.
// - a wait for pattern 0 on a timer, ended by a cancel of the timer. A waiter thread waits, the main
//   thread cancels once the waiter waits, and the waiter prints what it got, with the out values.
//   The cancel prints its result and the count it wrote.
// - a wait with callbacks for pattern 0 on a simple event, with a timeout of 800 ms, which a callback
//   notify doesn't end. The callback runs during the wait, and the wait goes on until its timeout. It
//   prints the callback's runs, the result, whether it took at least 400 ms, and the out values.
// The main thread cancels the timer once the timer waiter waits. Every thread is joined before its
// values are printed.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define OUT_MARKER 0xDEADBEEF
#define USER_DATA_MARKER 0xCAFEF00DBAADF00Dull
#define EVENT_ALL_BITS 0xFF000000
#define ZERO_TIMEOUT_US 600000
#define ZERO_LONG_US 300000
#define CB_TIMEOUT_US 800000
#define CB_LONG_US 400000

static SceUID timer, event, callback;
static volatile int callback_ready, callback_runs;
static volatile int waiter_result;
static volatile SceUInt32 waiter_pattern;
static volatile SceUInt64 waiter_user_data;
static volatile SceUInt64 waiter_elapsed_us;

// Waits for pattern 0 on the timer, with a timeout of 3 s
static int timer_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 result = OUT_MARKER;
    SceUInt64 user_data = USER_DATA_MARKER;
    SceUInt32 timeout = TEST_WAIT_US;
    waiter_result = sceKernelWaitEvent(timer, 0, &result, &user_data, &timeout);
    waiter_pattern = result;
    waiter_user_data = user_data;
    return 0;
}

// Waits for pattern 0 on the event with callbacks, with a callback that counts
// its runs
static int callback_counter(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    return 0;
}

static int callback_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("zero", 0, callback_counter, NULL);
    callback_ready = 1;
    SceUInt32 result = OUT_MARKER;
    SceUInt64 user_data = USER_DATA_MARKER;
    SceUInt32 timeout = CB_TIMEOUT_US;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    waiter_result = sceKernelWaitEventCB(event, 0, &result, &user_data, &timeout);
    waiter_elapsed_us = sceKernelGetProcessTimeWide() - start;
    waiter_pattern = result;
    waiter_user_data = user_data;
    sceKernelDeleteCallback(callback);
    return 0;
}

static void print_out(const char *what) {
    PRINTF(waiter_pattern, "%s: the result pattern", what);
    PRINTF((SceUInt32)waiter_user_data, "%s: the user data, low word", what);
    PRINTF((SceUInt32)(waiter_user_data >> 32), "%s: the user data, high word",
        what);
}

static void timer_timeout_case(void) {
    timer = sceKernelCreateTimer("zero", 0, NULL);
    SceUInt32 result = OUT_MARKER;
    SceUInt64 user_data = USER_DATA_MARKER;
    SceUInt32 timeout = ZERO_TIMEOUT_US;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    const int waited = sceKernelWaitEvent(timer, 0, &result, &user_data, &timeout);
    const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - start;
    PRINT("timer, pattern 0 with a timeout: the wait", waited);
    PRINT("timer, pattern 0 with a timeout: it took at least 300 ms",
        elapsed >= ZERO_LONG_US);
    PRINTF(result, "timer, pattern 0 with a timeout: the result pattern");
    PRINTF((SceUInt32)user_data,
        "timer, pattern 0 with a timeout: the user data, low word");
    PRINTF((SceUInt32)(user_data >> 32),
        "timer, pattern 0 with a timeout: the user data, high word");
    sceKernelDeleteTimer(timer);
}

static void timer_cancel_case(void) {
    timer = sceKernelCreateTimer("zero", 0, NULL);
    const SceUID thread = test_start_thread(timer_waiter);
    test_wait_until_waiting(thread);
    PRINT("timer, pattern 0 and a cancel: the waiter waits for the timer",
        test_in_wait(thread, WAIT_TYPE_EVENT, timer));
    SceUInt32 waiting = OUT_MARKER;
    PRINT("timer, pattern 0 and a cancel: the cancel",
        sceKernelCancelTimer(timer, &waiting));
    PRINT("timer, pattern 0 and a cancel: the count the cancel wrote", waiting);
    test_join_thread(thread);
    PRINT("timer, pattern 0 and a cancel: the wait", waiter_result);
    print_out("timer, pattern 0 and a cancel");
    sceKernelDeleteTimer(timer);
}

static void callback_case(void) {
    event = sceKernelCreateSimpleEvent("zero", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    callback_ready = callback_runs = 0;
    const SceUID thread = test_start_thread(callback_waiter);
    TEST_WAIT_FOR(callback_ready && test_in_wait(thread, WAIT_TYPE_EVENT | WAIT_TYPE_CB, event));
    PRINT("callback, pattern 0: the notify",
        sceKernelNotifyCallback(callback, 1));
    TEST_WAIT_FOR(callback_runs != 0);
    test_join_thread(thread);
    PRINT("callback, pattern 0: runs of the callback during the wait",
        callback_runs);
    PRINT("callback, pattern 0: the wait", waiter_result);
    PRINT("callback, pattern 0: it took at least 400 ms",
        waiter_elapsed_us >= CB_LONG_US);
    print_out("callback, pattern 0");
    sceKernelDeleteSimpleEvent(event);
}

int main(void) {
    timer_timeout_case();
    timer_cancel_case();
    callback_case();

    test_exit();
    return 0;
}
