#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static SceUID mutex, cond;
static int wait_result[2], unlock_result[2];

static int wait_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    wait_result[i] = sceKernelWaitCond(cond, NULL);
    unlock_result[i] = sceKernelUnlockMutex(mutex, 1);
    return 0;
}

// Starts a thread that waits on the condition into slot i of the results
static SceUID start_waiter(int i) {
    wait_result[i] = unlock_result[i] = -1;
    const SceUID thread = test_start_thread_arg(wait_thread, i);
    test_wait_until_waiting(thread);
    return thread;
}

int main(void) {
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);

    PRINT("signal with no waiters", sceKernelSignalCond(cond));

    sceKernelLockMutex(mutex, 1, NULL);
    unsigned int timeout = 1000;
    PRINT("wait times out", sceKernelWaitCond(cond, &timeout));
    PRINT("a timed out wait owns the mutex again", sceKernelUnlockMutex(mutex, 1));

    // A waiter owns the mutex again when its wait ends
    SceUID waiter = start_waiter(0);
    sceKernelLockMutex(mutex, 1, NULL);
    PRINT("signal", sceKernelSignalCond(cond));
    sceKernelUnlockMutex(mutex, 1);
    test_join_thread(waiter);
    PRINT("a signal ends a wait", wait_result[0]);
    PRINT("the waiter owns the mutex after the wait", unlock_result[0]);

    const SceUID first = start_waiter(0);
    const SceUID second = start_waiter(1);
    PRINT("signal all", sceKernelSignalCondAll(cond));
    test_join_thread(first);
    test_join_thread(second);
    PRINT("the first of two waiters", wait_result[0]);
    PRINT("the second of two waiters", wait_result[1]);

    waiter = start_waiter(0);
    PRINT("delete", sceKernelDeleteCond(cond));
    test_join_thread(waiter);
    PRINT("a wait on a deleted condition", wait_result[0]);

    test_exit();
    return 0;
}
