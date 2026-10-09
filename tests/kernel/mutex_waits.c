// Mutex waits across threads: what ends them, who gets the mutex, and the time left
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define LOW_PRIORITY 0x10000102
#define HIGH_PRIORITY 0x100000FE

static SceUID mutex;
static SceKernelLwMutexWork lw_mutex;
static int lock_result[2], order[2], woken;
static SceUInt time_left;

// Locks the mutex and ends without unlocking it
static int lock_and_end_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    return 0;
}

// Locks the mutex, holds it for a while, then ends without unlocking it
static int hold_and_end_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    sceKernelDelayThread(30000);
    return 0;
}

// Waits for the mutex into slot i, and unlocks it again if it got it
static int wait_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = 500000;
    lock_result[i] = sceKernelLockMutex(mutex, 1, &timeout);
    if (lock_result[i] == 0) {
        order[i] = ++woken;
        time_left = timeout;
        sceKernelUnlockMutex(mutex, 1);
    }
    return 0;
}

static SceUID start_waiter(int i, int priority) {
    lock_result[i] = -1;
    order[i] = 0;
    const SceUID thread = sceKernelCreateThread("waiter", wait_thread, priority, 0x4000, 0, 0, NULL);
    sceKernelStartThread(thread, sizeof(i), &i);
    test_wait_until_waiting(thread);
    return thread;
}

static int lw_try_result, lw_lock_result;

static int lw_other_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    lw_try_result = sceKernelTryLockLwMutex(&lw_mutex, 1);
    SceUInt timeout = 500000;
    lw_lock_result = sceKernelLockLwMutex(&lw_mutex, 1, &timeout);
    if (lw_lock_result == 0)
        sceKernelUnlockLwMutex(&lw_mutex, 1);
    return 0;
}

int main(void) {
    // Whether a mutex is unlocked when the thread that holds it ends
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    test_join_thread(test_start_thread(lock_and_end_thread));
    PRINT("the mutex of an ended thread is free", sceKernelTryLockMutex(mutex, 1));
    sceKernelUnlockMutex(mutex, 1);

    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    const SceUID holder = test_start_thread(hold_and_end_thread);
    test_wait_until_waiting(holder);
    SceUID waiter = start_waiter(0, 0x10000100);
    test_join_thread(holder);
    test_join_thread(waiter);
    PRINT("a waiter gets the mutex when its owner ends", lock_result[0]);

    // The waiter gets the time it had left. A new mutex, since an ended owner may still hold the old one.
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    sceKernelLockMutex(mutex, 1, NULL);
    waiter = start_waiter(0, 0x10000100);
    sceKernelUnlockMutex(mutex, 1);
    test_join_thread(waiter);
    PRINT("a waiter that got the mutex has time left", time_left > 0 && time_left < 500000);

    // Deleting ends the wait
    sceKernelLockMutex(mutex, 1, NULL);
    waiter = start_waiter(0, 0x10000100);
    PRINT("delete", sceKernelDeleteMutex(mutex));
    test_join_thread(waiter);
    PRINT("a wait on a deleted mutex", lock_result[0]);

    // The priority attribute hands the mutex to the higher priority waiter first
    mutex = sceKernelCreateMutex("prio", ATTR_TH_PRIO, 1, NULL);
    woken = 0;
    const SceUID low = start_waiter(0, LOW_PRIORITY);
    const SceUID high = start_waiter(1, HIGH_PRIORITY);
    sceKernelUnlockMutex(mutex, 1);
    test_join_thread(low);
    test_join_thread(high);
    PRINT("the higher priority waiter gets the mutex first", order[1]);
    PRINT("the lower priority one after it", order[0]);

    // A lightweight mutex held by one thread can't be taken by another until it is unlocked
    sceKernelCreateLwMutex(&lw_mutex, "lw", 0, 1, NULL);
    waiter = test_start_thread(lw_other_thread);
    test_wait_until_waiting(waiter);
    sceKernelUnlockLwMutex(&lw_mutex, 1);
    test_join_thread(waiter);
    PRINT("try lock of a lightweight mutex another thread holds", lw_try_result);
    PRINT("the waiter gets it when it is unlocked", lw_lock_result);

    test_exit();
    return 0;
}
