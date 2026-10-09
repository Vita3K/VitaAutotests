// Lock count and ownership checks of heavy mutexes
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID held, release;
static int other_unlock, other_lock, waiter_lock;

// Waits for the held mutex, then keeps it until release is signaled
static int waiter_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 500000;
    waiter_lock = sceKernelLockMutex(held, 1, &timeout);
    if (waiter_lock == 0) {
        timeout = 500000;
        sceKernelWaitSema(release, 1, &timeout);
        sceKernelUnlockMutex(held, 1);
    }
    return 0;
}

static int other_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 0;
    other_unlock = sceKernelUnlockMutex(held, 1);
    other_lock = sceKernelLockMutex(held, 1, &timeout);
    return 0;
}

int main(void) {
    PRINT("create with a negative count", test_created(sceKernelCreateMutex("m", 0, -1, NULL)));
    PRINT("create non-recursive with a count of 2", test_created(sceKernelCreateMutex("m", 0, 2, NULL)));
    const SceUID recursive_held = sceKernelCreateMutex("m", MUTEX_ATTR_RECURSIVE, 2, NULL);
    PRINT("create recursive with a count of 2", recursive_held > 0);

    const SceUID mutex = sceKernelCreateMutex("m", 0, 0, NULL);
    PRINT("lock with a count of 0", sceKernelLockMutex(mutex, 0, NULL));
    PRINT("lock with a negative count", sceKernelLockMutex(mutex, -1, NULL));
    PRINT("lock non-recursive with a count of 2", sceKernelLockMutex(mutex, 2, NULL));
    PRINT("unlock while not locked", sceKernelUnlockMutex(mutex, 1));
    PRINT("lock", sceKernelLockMutex(mutex, 1, NULL));
    PRINT("lock again without recursive", sceKernelLockMutex(mutex, 1, NULL));
    PRINT("try lock again without recursive", sceKernelTryLockMutex(mutex, 1));
    PRINT("unlock with a count of 0", sceKernelUnlockMutex(mutex, 0));
    PRINT("unlock non-recursive with a count of 2", sceKernelUnlockMutex(mutex, 2));
    PRINT("unlock", sceKernelUnlockMutex(mutex, 1));

    const SceUID recursive = sceKernelCreateMutex("m", MUTEX_ATTR_RECURSIVE, 0, NULL);
    PRINT("lock recursive with a count of 2", sceKernelLockMutex(recursive, 2, NULL));
    PRINT("lock recursive again", sceKernelLockMutex(recursive, 1, NULL));
    PRINT("lock recursive past the maximum count", sceKernelLockMutex(recursive, 0x7FFFFFFF, NULL));
    PRINT("unlock more than locked", sceKernelUnlockMutex(recursive, 4));
    PRINT("unlock all of it", sceKernelUnlockMutex(recursive, 3));

    // Another thread can't unlock this thread's mutex, and a zero timeout gives up at once
    held = mutex;
    sceKernelLockMutex(held, 1, NULL);
    test_join_thread(test_start_thread(other_thread));
    PRINT("unlock by a thread that doesn't own it", other_unlock);
    PRINT("lock with a zero timeout while owned", other_lock);

    // Unlocking hands the mutex to the waiter
    release = sceKernelCreateSema("release", 0, 0, 1, NULL);
    waiter_lock = -1;
    SceUID waiter = test_start_thread(waiter_thread);
    test_wait_until_waiting(waiter);
    sceKernelUnlockMutex(held, 1);
    test_let_threads_wait();
    PRINT("the waiter gets the mutex when it is unlocked", waiter_lock);
    PRINT("the waiter owns it now", sceKernelTryLockMutex(held, 1));
    sceKernelSignalSema(release, 1);
    test_join_thread(waiter);

    // Canceling ends the waits, and leaves the mutex unowned or owned by the caller with the new count
    int waiting = -1;
    sceKernelLockMutex(held, 1, NULL);
    waiter_lock = -1;
    waiter = test_start_thread(waiter_thread);
    test_wait_until_waiting(waiter);
    PRINT("cancel to a count of 0", sceKernelCancelMutex(held, 0, &waiting));
    test_join_thread(waiter);
    PRINT("cancel counts the waiting threads", waiting);
    PRINT("a canceled wait", waiter_lock);
    PRINT("cancel to 0 leaves the mutex unowned", sceKernelTryLockMutex(held, 1));
    sceKernelUnlockMutex(held, 1);
    PRINT("cancel to a count of 2", sceKernelCancelMutex(recursive, 2, &waiting));
    PRINT("the caller owns it with that count", sceKernelUnlockMutex(recursive, 2));
    PRINT("and not more", sceKernelUnlockMutex(recursive, 1));

    test_exit();
    return 0;
}
