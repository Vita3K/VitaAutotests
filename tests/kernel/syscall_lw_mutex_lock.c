// The thread manager's lightweight mutex lock call, which libkernel reaches after its own take fails,
// with the fourth argument that says whether callbacks run during the wait (0 for
// sceKernelLockLwMutex, 1 for sceKernelLockLwMutexCB). A waiter's callback is notified while it waits.
#include "syscalls.h"
#include "test.h"

static SceKernelLwMutexWork work;
static SceUID callback;
static volatile int callback_runs;
static volatile int runs_at_return, lock_result;
static int waiter_callbacks;

static int count_callback(int notify_id, int notify_count, int notify_arg, void *common) {
    (void)notify_id;
    (void)notify_count;
    (void)notify_arg;
    (void)common;
    callback_runs++;
    return 0;
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, count_callback, NULL);
    SceUInt32 timeout = TEST_WAIT_US;
    lock_result = syscall_lock_lw_mutex(&work, 1, &timeout, waiter_callbacks);
    runs_at_return = callback_runs;
    if (lock_result == 0)
        sceKernelUnlockLwMutex(&work, 1);
    // Runs the notified callback now if the wait didn't
    sceKernelCheckCallback();
    sceKernelDeleteCallback(callback);
    return 0;
}

static void contended(int callbacks) {
    const char *what = callbacks ? "with callbacks" : "without callbacks";
    callback_runs = 0;
    callback = 0;
    waiter_callbacks = callbacks;
    sceKernelLockLwMutex(&work, 1, NULL);
    const SceUID thread = test_start_thread(waiter);
    TEST_WAIT_FOR(callback != 0);
    test_wait_until_waiting(thread);
    sceKernelNotifyCallback(callback, 1);
    // A wait that runs callbacks runs this one before the unlock
    if (callbacks)
        TEST_WAIT_FOR(callback_runs != 0);
    sceKernelUnlockLwMutex(&work, 1);
    test_join_thread(thread);
    PRINTF(lock_result, "%s: the waiter's lock", what);
    PRINTF(runs_at_return, "%s: callback runs when the lock returned", what);
    PRINTF(callback_runs, "%s: callback runs in the end", what);
}

int main(void) {
    sceKernelCreateLwMutex(&work, "lw", 0, 0, NULL);

    PRINT("lock a free mutex", syscall_lock_lw_mutex(&work, 1, NULL, 0));
    PRINT("unlock it", sceKernelUnlockLwMutex(&work, 1));
    PRINT("lock a free mutex with callbacks", syscall_lock_lw_mutex(&work, 1, NULL, 1));
    PRINT("unlock it again", sceKernelUnlockLwMutex(&work, 1));

    contended(0);
    contended(1);

    sceKernelDeleteLwMutex(&work);
    test_exit();
    return 0;
}
