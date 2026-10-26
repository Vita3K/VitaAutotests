// Waits for pattern 0 on a message pipe's UID, which end by their timeout, by a cancel of the pipe
// or by its delete. simple_event_edges and simple_event_zero_pattern_more run the same pattern 0
// waits on a simple event and on a timer, and this test runs them on a pipe. Out values start as
// markers, so a value a call wrote shows apart from one it left alone. The cases:
// - a wait for pattern 0 on a new pipe with a timeout of 600 ms, which nothing ends. It prints the
//   result, the out values, and whether it took at least 300 ms, halfway between a wait that ends
//   at once and the timeout.
// - a waiter with a timeout of 1 s, and the main thread cancels the pipe once the waiter waits. It
//   prints the cancel's result and the counts it wrote, the waiter's result and out values, and
//   whether the waiter returned within 500 ms of the cancel, which an end at the timeout doesn't.
// - a waiter with a timeout of 5 s, and the main thread deletes the pipe once the waiter waits. It
//   prints the delete's result, the waiter's result and out values, and whether the waiter returned
//   within 2.5 s of the delete, which an end at the timeout doesn't.
// Whether the waiter waits is read before the cancel or delete and printed after the waiter ended.
// Every waiter is joined before its values are printed, and the time it returned is read after its
// join.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define OUT_MARKER 0xDEADBEEF
#define USER_DATA_MARKER 0xCAFEF00DBAADF00Dull
#define CAPACITY 4096
#define ZERO_TIMEOUT_US 600000
#define ZERO_HALF_US 300000
#define CANCEL_TIMEOUT_US 1000000
#define CANCEL_SOON_US 500000
#define DELETE_TIMEOUT_US 5000000
#define DELETE_SOON_US 2500000

static SceUID pipe;
static volatile int waiter_result;
static volatile SceUInt32 waiter_pattern;
static volatile SceUInt64 waiter_user_data;
static volatile SceUInt64 waiter_end;

// Waits for pattern 0 on the pipe with the timeout in microseconds passed as its argument
static int pattern_waiter(SceSize args, void *argp) {
    (void)args;
    const SceUInt32 timeout_us = *(const int *)argp;
    SceUInt32 result = OUT_MARKER;
    SceUInt64 user_data = USER_DATA_MARKER;
    SceUInt32 timeout = timeout_us;
    waiter_result = sceKernelWaitEvent(pipe, 0, &result, &user_data, &timeout);
    waiter_end = sceKernelGetProcessTimeWide();
    waiter_pattern = result;
    waiter_user_data = user_data;
    return 0;
}

// Prints the result pattern and the user data a wait wrote, each labeled with what
static void print_out(const char *what, SceUInt32 pattern, SceUInt64 user_data) {
    PRINTF(pattern, "%s: the result pattern", what);
    PRINTF((SceUInt32)user_data, "%s: the user data, low word", what);
    PRINTF((SceUInt32)(user_data >> 32), "%s: the user data, high word", what);
}

static void timeout_case(void) {
    pipe = sceKernelCreateMsgPipe("zero", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    SceUInt32 result = OUT_MARKER;
    SceUInt64 user_data = USER_DATA_MARKER;
    SceUInt32 timeout = ZERO_TIMEOUT_US;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    const int waited = sceKernelWaitEvent(pipe, 0, &result, &user_data, &timeout);
    const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - start;
    PRINT("timeout: the wait", waited);
    print_out("timeout", result, user_data);
    PRINT("timeout: it took at least 300 ms", elapsed >= ZERO_HALF_US);
    sceKernelDeleteMsgPipe(pipe);
}

static void cancel_case(void) {
    pipe = sceKernelCreateMsgPipe("zero", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    const SceUID thread = test_start_thread_arg(pattern_waiter, CANCEL_TIMEOUT_US);
    test_wait_until_waiting(thread);
    const int waiting = test_in_wait(thread, WAIT_TYPE_EVENT, pipe);
    int waiting_senders = (int)OUT_MARKER, waiting_receivers = (int)OUT_MARKER;
    const SceUInt64 action = sceKernelGetProcessTimeWide();
    PRINT("cancel: the cancel", sceKernelCancelMsgPipe(pipe, &waiting_senders, &waiting_receivers));
    PRINT("cancel: the senders the cancel counted", waiting_senders);
    PRINT("cancel: the receivers the cancel counted", waiting_receivers);
    test_join_thread(thread);
    PRINT("cancel: the waiter waits on the pipe", waiting);
    PRINT("cancel: the wait", waiter_result);
    print_out("cancel", waiter_pattern, waiter_user_data);
    PRINT("cancel: the waiter returned within 500 ms of the cancel", waiter_end - action < CANCEL_SOON_US);
    sceKernelDeleteMsgPipe(pipe);
}

static void delete_case(void) {
    pipe = sceKernelCreateMsgPipe("zero", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    const SceUID thread = test_start_thread_arg(pattern_waiter, DELETE_TIMEOUT_US);
    test_wait_until_waiting(thread);
    const int waiting = test_in_wait(thread, WAIT_TYPE_EVENT, pipe);
    const SceUInt64 action = sceKernelGetProcessTimeWide();
    PRINT("delete: the delete", sceKernelDeleteMsgPipe(pipe));
    test_join_thread(thread);
    PRINT("delete: the waiter waits on the pipe", waiting);
    PRINT("delete: the wait", waiter_result);
    print_out("delete", waiter_pattern, waiter_user_data);
    PRINT("delete: the waiter returned within 2.5 s of the delete", waiter_end - action < DELETE_SOON_US);
}

int main(void) {
    timeout_case();
    cancel_case();
    delete_case();

    test_exit();
    return 0;
}
