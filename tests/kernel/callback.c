#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static int calls, last_count, last_arg;
static SceUID last_thread;

static int callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)common;
    calls++;
    last_count = count;
    last_arg = arg;
    last_thread = sceKernelGetThreadId();
    return 0;
}

static SceUID other_cb, other_sema;
static int other_wait_result, calls_before_check;

// Creates a callback of its own and waits in a wait that checks callbacks
static int cb_wait_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    other_cb = sceKernelCreateCallback("other", 0, callback, NULL);
    SceUInt timeout = 500000;
    other_wait_result = sceKernelWaitSemaCB(other_sema, 1, &timeout);
    sceKernelDeleteCallback(other_cb);
    return 0;
}

// Creates a callback of its own and waits in a wait that doesn't check callbacks
static int plain_wait_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    other_cb = sceKernelCreateCallback("other", 0, callback, NULL);
    SceUInt timeout = 100000;
    other_wait_result = sceKernelWaitSema(other_sema, 1, &timeout);
    calls_before_check = calls;
    sceKernelCheckCallback();
    sceKernelDeleteCallback(other_cb);
    return 0;
}

int main(void) {
    const SceUID cb = sceKernelCreateCallback("callback", 0, callback, NULL);

    // Notifications add up until the callback runs, which gets their count and the last argument
    sceKernelNotifyCallback(cb, 1);
    sceKernelNotifyCallback(cb, 2);
    PRINT("two notifications are pending", sceKernelGetCallbackCount(cb));
    PRINT("nothing runs before a check", calls);
    sceKernelCheckCallback();
    PRINT("a check runs the callback once", calls);
    PRINT("the callback gets the notification count", last_count);
    PRINT("the callback gets the last argument", last_arg);
    PRINT("nothing is pending after it runs", sceKernelGetCallbackCount(cb));
    sceKernelCheckCallback();
    PRINT("a check with nothing pending runs nothing", calls);

    sceKernelNotifyCallback(cb, 3);
    PRINT("cancel", sceKernelCancelCallback(cb));
    PRINT("cancel drops the notification", sceKernelGetCallbackCount(cb));
    sceKernelCheckCallback();
    PRINT("a canceled notification doesn't run", calls);

    // Waits that check callbacks run them
    sceKernelNotifyCallback(cb, 4);
    sceKernelDelayThreadCB(1000);
    PRINT("a delay that checks callbacks runs them", calls);
    PRINT("the delay passes the argument", last_arg);

    sceKernelNotifyCallback(cb, 5);
    const SceUID sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    SceUInt timeout = 1000;
    PRINT("a wait that checks callbacks still times out", sceKernelWaitSemaCB(sema, 1, &timeout));
    PRINT("the wait runs the callback", calls);

    PRINT("delete", sceKernelDeleteCallback(cb));
    PRINT("notify a deleted callback", sceKernelNotifyCallback(cb, 6));

    // A callback runs on the thread that created it, during its wait, and the wait goes on after it
    other_sema = sceKernelCreateSema("other", 0, 0, 1, NULL);
    calls = 0;
    SceUID thread = test_start_thread(cb_wait_thread);
    test_wait_until_waiting(thread);
    sceKernelNotifyCallback(other_cb, 7);
    test_let_threads_wait();
    PRINT("the waiting thread runs it", calls);
    PRINT("on its own thread", last_thread == thread);
    sceKernelSignalSema(other_sema, 1);
    test_join_thread(thread);
    PRINT("the wait goes on after the callback", other_wait_result);

    // A wait that doesn't check callbacks leaves them pending
    calls = 0;
    thread = test_start_thread(plain_wait_thread);
    test_wait_until_waiting(thread);
    sceKernelNotifyCallback(other_cb, 8);
    test_join_thread(thread);
    PRINT("the wait times out", other_wait_result);
    PRINT("the callback didn't run during the wait", calls_before_check);
    PRINT("it ran at the next check", calls);

    test_exit();
    return 0;
}
