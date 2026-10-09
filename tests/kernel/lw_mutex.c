// Lock count and ownership checks of lightweight mutexes
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceKernelLwMutexWork held;
static int other_unlock, other_lock;

static int other_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 0;
    other_unlock = sceKernelUnlockLwMutex(&held, 1);
    other_lock = sceKernelLockLwMutex(&held, 1, &timeout);
    return 0;
}

int main(void) {
    SceKernelLwMutexWork work;
    PRINT("create with a negative count", sceKernelCreateLwMutex(&work, "m", 0, -1, NULL));
    PRINT("create non-recursive with a count of 2", sceKernelCreateLwMutex(&work, "m", 0, 2, NULL));
    SceKernelLwMutexWork recursive_held;
    PRINT("create recursive with a count of 2", sceKernelCreateLwMutex(&recursive_held, "m", MUTEX_ATTR_RECURSIVE, 2, NULL));

    SceKernelLwMutexWork mutex;
    sceKernelCreateLwMutex(&mutex, "m", 0, 0, NULL);
    PRINT("lock with a count of 0", sceKernelLockLwMutex(&mutex, 0, NULL));
    PRINT("lock with a negative count", sceKernelLockLwMutex(&mutex, -1, NULL));
    PRINT("lock non-recursive with a count of 2", sceKernelLockLwMutex(&mutex, 2, NULL));
    PRINT("unlock while not locked", sceKernelUnlockLwMutex(&mutex, 1));
    PRINT("lock", sceKernelLockLwMutex(&mutex, 1, NULL));
    PRINT("lock again without recursive", sceKernelLockLwMutex(&mutex, 1, NULL));
    PRINT("try lock again without recursive", sceKernelTryLockLwMutex(&mutex, 1));
    PRINT("unlock with a count of 0", sceKernelUnlockLwMutex(&mutex, 0));
    PRINT("unlock non-recursive with a count of 2", sceKernelUnlockLwMutex(&mutex, 2));
    PRINT("unlock", sceKernelUnlockLwMutex(&mutex, 1));

    SceKernelLwMutexWork recursive;
    sceKernelCreateLwMutex(&recursive, "m", MUTEX_ATTR_RECURSIVE, 0, NULL);
    PRINT("lock recursive with a count of 2", sceKernelLockLwMutex(&recursive, 2, NULL));
    PRINT("lock recursive again", sceKernelLockLwMutex(&recursive, 1, NULL));
    PRINT("lock recursive past the maximum count", sceKernelLockLwMutex(&recursive, 0x7FFFFFFF, NULL));
    PRINT("unlock more than locked", sceKernelUnlockLwMutex(&recursive, 4));
    PRINT("unlock all of it", sceKernelUnlockLwMutex(&recursive, 3));

    // Another thread can't unlock this thread's mutex, and a zero timeout gives up at once
    sceKernelCreateLwMutex(&held, "held", 0, 1, NULL);
    test_join_thread(test_start_thread(other_thread));
    PRINT("unlock by a thread that doesn't own it", other_unlock);
    PRINT("lock with a zero timeout while owned", other_lock);
    sceKernelUnlockLwMutex(&held, 1);

    test_exit();
    return 0;
}
