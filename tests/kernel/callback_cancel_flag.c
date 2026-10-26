// The notify flag that the thread info reports for a thread that waits without callbacks, through
// a notify, a cancel and a delete of its callbacks, and what a wait with callbacks does after a
// cancel left the flag set. Each case has a fresh thread, which creates its callbacks and waits in
// a plain semaphore wait with a long timeout. The main thread notifies, cancels and deletes them
// while the thread is blocked, and reads the thread info after each step. The cases:
// - a callback notified, cancelled and deleted, then a second one notified and deleted while it is
//   pending
// - two callbacks notified and one of them cancelled, then a check that runs the other one, a flag
//   read after the check, then a third callback notified and cancelled, and a semaphore wait with
//   callbacks. The case checks that the wait runs nothing and ends by its timeout of 2 s, and it
//   reads the flag that the thread leaves behind after the wait.
// The thread is released by a signal of the semaphore, so the plain waits end by it and the wait
// with callbacks ends by its timeout.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// How long a plain wait lasts, long enough for the main thread to make all its calls before a wake
#define PLAIN_WAIT_US 6000000

// The timeout of the wait with callbacks at the end of the second case
#define CB_WAIT_US 2000000

// One slot per callback of a case, which the callbacks count their runs in
#define SLOTS 5

static SceUID sema;
static SceUID cb[SLOTS];
static int slot_ids[SLOTS] = { 0, 1, 2, 3, 4 };
static volatile int runs[SLOTS], run_count[SLOTS], run_arg[SLOTS];
static volatile int checked, plain_result, check_result, plain_again_result;
static volatile int cb_wait_result, cb_wait_time_left, cb_wait_whole, cb_wait_flag;

// Counts a run in the slot that common points to, and keeps the count and argument of the last run
static int counting_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    const int slot = *(const int *)common;
    runs[slot]++;
    run_count[slot] = count;
    run_arg[slot] = arg;
    return 0;
}

// Returns the notify flag that the thread info reports for thread
static int notify_flag(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.fNotifyCallback;
}

// Creates two callbacks, then waits in a plain wait until the main thread signals the semaphore
static int notify_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    cb[0] = sceKernelCreateCallback("first", 0, counting_callback, &slot_ids[0]);
    cb[1] = sceKernelCreateCallback("second", 0, counting_callback, &slot_ids[1]);
    SceUInt timeout = PLAIN_WAIT_US;
    plain_result = sceKernelWaitSema(sema, 1, &timeout);
    return 0;
}

// Creates three callbacks, then waits in a plain wait for the main thread. It checks callbacks when
// the wait ends, sets checked, waits in a plain wait again, and then waits with callbacks.
static int check_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (int i = 2; i < SLOTS; i++)
        cb[i] = sceKernelCreateCallback("check", 0, counting_callback, &slot_ids[i]);
    SceUInt timeout = PLAIN_WAIT_US;
    plain_result = sceKernelWaitSema(sema, 1, &timeout);
    check_result = sceKernelCheckCallback();
    checked = 1;
    timeout = PLAIN_WAIT_US;
    plain_again_result = sceKernelWaitSema(sema, 1, &timeout);
    timeout = CB_WAIT_US;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    cb_wait_result = sceKernelWaitSemaCB(sema, 1, &timeout);
    // The wait lasted its whole timeout when the time it took is at least the timeout, less 250 ms
    cb_wait_whole = sceKernelGetProcessTimeWide() - start + 250000 >= CB_WAIT_US;
    cb_wait_time_left = timeout;
    cb_wait_flag = notify_flag(sceKernelGetThreadId());
    return 0;
}

// Notifies, cancels and deletes two callbacks of a thread that waits in a plain wait
static void notify_case(void) {
    sema = sceKernelCreateSema("notify", 0, 0, 1, NULL);
    const SceUID thread = test_start_thread(notify_thread);
    test_wait_until_waiting(thread);
    PRINT("one: the flag before any notify", notify_flag(thread));
    PRINTF(sceKernelNotifyCallback(cb[0], 0x11), "one: the notify of the first callback");
    PRINT("one: the flag after the notify", notify_flag(thread));
    PRINTF(sceKernelCancelCallback(cb[0]), "one: the cancel of the first callback");
    PRINTF(sceKernelGetCallbackCount(cb[0]), "one: the count of the first callback after cancel");
    PRINT("one: the flag after the cancel", notify_flag(thread));
    PRINTF(sceKernelDeleteCallback(cb[0]), "one: the delete of the first callback");
    PRINT("one: the flag after the delete", notify_flag(thread));
    PRINTF(sceKernelNotifyCallback(cb[1], 0x21), "one: the notify of the second callback");
    PRINT("one: the flag after the second notify", notify_flag(thread));
    PRINTF(sceKernelDeleteCallback(cb[1]), "one: the delete of the second callback while pending");
    PRINT("one: the flag after deleting the pending callback", notify_flag(thread));
    PRINTF(sceKernelSignalSema(sema, 1), "one: the signal that releases the thread");
    test_join_thread(thread);
    PRINT("one: the plain wait's result", plain_result);
    sceKernelDeleteSema(sema);
}

// Notifies two callbacks of a waiting thread, cancels one, checks, then notifies and cancels a
// third callback and waits with callbacks
static void check_case(void) {
    sema = sceKernelCreateSema("check", 0, 0, 1, NULL);
    checked = 0;
    const SceUID thread = test_start_thread(check_thread);
    test_wait_until_waiting(thread);
    PRINT("check: the flag before any notify", notify_flag(thread));
    PRINTF(sceKernelNotifyCallback(cb[2], 0x21), "check: the notify of the first callback");
    PRINTF(sceKernelNotifyCallback(cb[3], 0x22), "check: the notify of the second callback");
    PRINT("check: the flag after two notifies", notify_flag(thread));
    PRINTF(sceKernelCancelCallback(cb[2]), "check: the cancel of the first callback");
    PRINTF(sceKernelGetCallbackCount(cb[2]), "check: the count of the first callback after cancel");
    PRINTF(sceKernelGetCallbackCount(cb[3]), "check: the count of the second callback");
    PRINT("check: the flag after the cancel", notify_flag(thread));
    PRINTF(sceKernelSignalSema(sema, 1), "check: the signal that releases the thread");
    TEST_WAIT_FOR(checked);
    test_wait_until_waiting(thread);
    PRINT("check: the check's result", check_result);
    PRINT("check: runs of the cancelled callback", runs[2]);
    PRINT("check: runs of the notified callback", runs[3]);
    PRINT("check: the count the notified callback ran with", run_count[3]);
    PRINT("check: the argument the notified callback ran with", run_arg[3]);
    PRINT("check: the flag after the check", notify_flag(thread));
    PRINTF(sceKernelNotifyCallback(cb[4], 0x31), "stale: the notify of the third callback");
    PRINTF(sceKernelCancelCallback(cb[4]), "stale: the cancel of the third callback");
    PRINTF(sceKernelGetCallbackCount(cb[4]), "stale: the count of the third callback after cancel");
    PRINT("stale: the flag after the cancel", notify_flag(thread));
    PRINTF(sceKernelSignalSema(sema, 1), "stale: the signal that moves the thread to the next wait");
    test_join_thread(thread);
    PRINT("check: the plain wait's result", plain_result);
    PRINT("check: the plain wait's result after the check", plain_again_result);
    PRINT("stale: the wait with callbacks ends by its timeout", cb_wait_result);
    PRINT("stale: its time left", cb_wait_time_left);
    PRINT("stale: it lasted its whole timeout", cb_wait_whole);
    PRINT("stale: the flag the thread reads after the wait", cb_wait_flag);
    PRINT("stale: runs of the third callback", runs[4]);
    sceKernelDeleteSema(sema);
}

int main(void) {
    notify_case();
    check_case();

    test_exit();
    return 0;
}
