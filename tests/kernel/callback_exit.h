// Shared by callback_exit_delay, callback_exit_delete_delay, callback_exit_check and
// callback_exit_delete_check. A target thread T creates a callback that calls sceKernelExitThread or
// sceKernelExitDeleteThread, and runs it in one of two ways: in a long delay with callbacks, which
// runs it once notified, or with sceKernelCheckCallback, called after the notify while T was in a
// wait without callbacks. The main thread prints a line that it notifies the callback, notifies it,
// lets T go on, and waits for T's end with a timeout well before the end of T's delay. Only once
// that wait returned does main print the results, T's exit status, whether the exit call returned,
// and a last line that shows it went on. So an exit that ends the app leaves the output at the line
// before the notify.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

typedef enum RunBy {
    BY_DELAY,
    BY_CHECK
} RunBy;

static RunBy run_by;
static int use_delete;
static SceUID callback, go;
static volatile int callback_made, exit_returned;

static int exit_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    if (use_delete)
        sceKernelExitDeleteThread(0x55);
    else
        sceKernelExitThread(0x55);
    exit_returned = 1;
    return 0;
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("exit", 0, exit_callback, NULL);
    callback_made = 1;
    if (run_by == BY_DELAY) {
        sceKernelDelayThreadCB(20 * TEST_WAIT_US);
    } else {
        sceKernelWaitSema(go, 1, NULL);
        sceKernelCheckCallback();
    }
    return 0x66;
}

static void exit_run(RunBy by, int delete) {
    run_by = by;
    use_delete = delete;
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    const SceUID target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    PRINT("start the target", sceKernelStartThread(target, 0, NULL));
    TEST_WAIT_FOR(callback_made);
    test_wait_until_waiting(target);
    PRINT("notify the callback", 1);
    const int notify = sceKernelNotifyCallback(callback, 1);
    const int signal = sceKernelSignalSema(go, 1);
    int status = 0xDEAD;
    SceUInt timeout = 5 * TEST_WAIT_US;
    const int wait = sceKernelWaitThreadEnd(target, &status, &timeout);
    PRINT("the notify", notify);
    PRINT("the signal", signal);
    PRINT("the wait for the target's end", wait);
    PRINT("its exit status", status);
    PRINT("the exit call returned", exit_returned);
    PRINT("the main thread goes on", 1);
}
