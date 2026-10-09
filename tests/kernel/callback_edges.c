// Callbacks that do things during a wait, and checks from inside a callback
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static SceUID sema, self_cb, other_cb;
static int calls, inner_check, inner_notify;

// Signals the semaphore its own thread waits on
static int signal_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    calls++;
    sceKernelSignalSema(sema, 1);
    return 0;
}

// Checks callbacks and notifies another one from inside a callback
static int nesting_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    calls++;
    inner_check = sceKernelCheckCallback();
    inner_notify = sceKernelNotifyCallback(other_cb, 1);
    return 0;
}

static int counting_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    calls++;
    return 0;
}

int main(void) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);

    // A callback that signals the semaphore of the wait it runs in
    self_cb = sceKernelCreateCallback("signal", 0, signal_callback, NULL);
    sceKernelNotifyCallback(self_cb, 0);
    SceUInt timeout = 100000;
    PRINT("a wait whose callback signals it", sceKernelWaitSemaCB(sema, 1, &timeout));
    PRINT("the callback ran", calls);
    sceKernelDeleteCallback(self_cb);

    // Checking and notifying from inside a callback
    calls = 0;
    self_cb = sceKernelCreateCallback("nesting", 0, nesting_callback, NULL);
    other_cb = sceKernelCreateCallback("other", 0, counting_callback, NULL);
    sceKernelNotifyCallback(self_cb, 0);
    PRINT("check that runs the nesting callback", sceKernelCheckCallback());
    PRINT("a check from inside a callback", inner_check);
    PRINT("a notify from inside a callback", inner_notify);
    PRINT("callbacks run by that one check", calls);
    PRINT("the other callback is still pending", sceKernelGetCallbackCount(other_cb));
    PRINT("the next check", sceKernelCheckCallback());
    PRINT("callbacks run after it", calls);

    // Two pending callbacks and one check
    calls = 0;
    sceKernelNotifyCallback(other_cb, 0);
    const SceUID third = sceKernelCreateCallback("third", 0, counting_callback, NULL);
    sceKernelNotifyCallback(third, 0);
    PRINT("a check with two callbacks pending", sceKernelCheckCallback());
    PRINT("callbacks run by it", calls);

    // A delay that runs a callback still lasts the whole delay
    calls = 0;
    sceKernelNotifyCallback(third, 0);
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    PRINT("a delay with callbacks", sceKernelDelayThreadCB(50000));
    PRINT("it lasts the whole delay", sceKernelGetProcessTimeWide() - start >= 50000);
    PRINT("callbacks run during it", calls);

    // Deleting a callback with a notification pending
    sceKernelNotifyCallback(third, 0);
    PRINT("delete a callback with a notification pending", sceKernelDeleteCallback(third));
    calls = 0;
    sceKernelCheckCallback();
    PRINT("the deleted callback doesn't run", calls);
    PRINT("cancel a deleted callback", sceKernelCancelCallback(third));

    test_exit();
    return 0;
}
