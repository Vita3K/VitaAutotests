// What _sceKernelExitCallback does when a callback calls it, which is how a callback ends. A thread
// waits with callbacks on a semaphore and the main thread notifies its callback. The callback sets
// a flag, calls _sceKernelExitCallback with a value, and sets a second flag. The value stands for
// the callback's return value. The test doesn't call it outside a callback. Every case has a new
// semaphore, thread and callback, and every wait has a timeout. The cases:
// - the callback exits with 7: the test prints the second flag, whether the thread waits again, the
//   callback's info and a notify of it after the run, then signals the semaphore and prints the
//   wait's result
// - the callback exits with 0: the same, then the test notifies the callback a second time and
//   prints how often it ran, the second flag and the info again, then signals the semaphore
//
// On a PS Vita TV on firmware 3.65 the call doesn't return, so the second flag stays 0 with both
// values. After the exit with 7 the callback is deleted, and both its info and a notify of it fail
// with 0x800280A1. After the exit with 0 the callback is kept, its info gives 0 and a second notify
// runs it again, so it has run twice. In both cases the thread still waits on the semaphore after
// the callback ended, and the signal then ends its wait with 0.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD

// Ends the calling callback with value as its return, like the return of a callback does
int _sceKernelExitCallback(int value);

static SceUID sema;
static volatile SceUID callback;
static volatile int exit_value, wait_result, runs, reached_flag, after_exit_flag;

static int exit_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    runs++;
    reached_flag = 1;
    _sceKernelExitCallback(exit_value);
    after_exit_flag = 1;
    return 0;
}

static int waiter_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("exit", 0, exit_callback, NULL);
    SceUInt timeout = 2 * TEST_WAIT_US;
    wait_result = sceKernelWaitSemaCB(sema, 1, &timeout);
    sceKernelDeleteCallback(callback);
    return 0;
}

static int in_cb_wait(SceUID thread) {
    return test_in_wait(thread, WAIT_TYPE_SEMA | WAIT_TYPE_CB, sema);
}

// Waits until the callback has run runs times and the thread waits again after it
static void wait_for_run(SceUID thread, int count) {
    TEST_WAIT_FOR(runs >= count);
    TEST_WAIT_FOR(in_cb_wait(thread));
}

static int callback_info_result(void) {
    CallbackInfo info = { .size = sizeof(info) };
    return sceKernelGetCallbackInfo(callback, (SceKernelCallbackInfo *)&info);
}

static SceUID start_case(int value) {
    exit_value = value;
    runs = reached_flag = after_exit_flag = 0;
    wait_result = NOT_DONE;
    callback = 0;
    sema = sceKernelCreateSema("exit", 0, 0, 1, NULL);
    const SceUID thread = test_start_thread(waiter_thread);
    TEST_WAIT_FOR(in_cb_wait(thread));
    return thread;
}

static void finish_case(const char *name, SceUID thread) {
    PRINTF(in_cb_wait(thread), "%s: the thread still waits before the signal", name);
    sceKernelSignalSema(sema, 1);
    test_join_thread(thread);
    PRINTF(wait_result, "%s: the wait", name);
    sceKernelDeleteSema(sema);
}

static void run_exit_nonzero(void) {
    const SceUID thread = start_case(7);
    PRINT("exit 7: the notify", sceKernelNotifyCallback(callback, 1));
    wait_for_run(thread, 1);
    PRINT("exit 7: the callback reached the call", reached_flag);
    PRINT("exit 7: the code after the call ran", after_exit_flag);
    PRINT("exit 7: runs", runs);
    PRINT("exit 7: the info", callback_info_result());
    PRINT("exit 7: the notify after", sceKernelNotifyCallback(callback, 1));
    finish_case("exit 7", thread);
}

static void run_exit_zero(void) {
    const SceUID thread = start_case(0);
    PRINT("exit 0: the notify", sceKernelNotifyCallback(callback, 1));
    wait_for_run(thread, 1);
    PRINT("exit 0: the callback reached the call", reached_flag);
    PRINT("exit 0: the code after the call ran", after_exit_flag);
    PRINT("exit 0: runs", runs);
    PRINT("exit 0: the info", callback_info_result());

    PRINT("exit 0: the second notify", sceKernelNotifyCallback(callback, 2));
    wait_for_run(thread, 2);
    PRINT("exit 0: runs after the second notify", runs);
    PRINT("exit 0: the code after the call ran in the second run", after_exit_flag);
    PRINT("exit 0: the info after the second run", callback_info_result());
    finish_case("exit 0", thread);
}

int main(void) {
    run_exit_nonzero();
    run_exit_zero();

    test_exit();
    return 0;
}
