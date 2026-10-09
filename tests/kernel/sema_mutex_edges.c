// Semaphore and mutex edge cases
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    SceUInt timeout;
    int waiting;

    // Semaphore creation
    PRINT("create with the count above the maximum", test_created(sceKernelCreateSema("s", 0, 3, 2, NULL)));
    PRINT("create with a maximum of 0", test_created(sceKernelCreateSema("s", 0, 0, 0, NULL)));
    PRINT("create with a negative count", test_created(sceKernelCreateSema("s", 0, -1, 2, NULL)));
    PRINT("create with a negative maximum", test_created(sceKernelCreateSema("s", 0, 0, -1, NULL)));
    PRINT("create with attribute 0xFFFF", test_created(sceKernelCreateSema("s", 0xFFFF, 0, 2, NULL)));

    // Semaphore counts
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 4, NULL);
    PRINT("signal 0", sceKernelSignalSema(sema, 0));
    PRINT("signal -1", sceKernelSignalSema(sema, -1));
    timeout = 1000;
    PRINT("wait for 0", sceKernelWaitSema(sema, 0, &timeout));
    timeout = 0;
    PRINT("wait with a timeout of 0 that is met", sceKernelWaitSema(sema, 1, &timeout));
    timeout = 0;
    PRINT("wait with a timeout of 0 that isn't met", sceKernelWaitSema(sema, 1, &timeout));
    waiting = 0xDEAD;
    PRINT("cancel to -1", sceKernelCancelSema(sema, -1, &waiting));
    PRINT("poll the initial count after it", sceKernelPollSema(sema, 1));
    PRINT("poll one more", sceKernelPollSema(sema, 1));
    PRINT("cancel without a count pointer", sceKernelCancelSema(sema, 2, NULL));
    PRINT("cancel to -2", sceKernelCancelSema(sema, -2, &waiting));

    // Mutexes
    const SceUID mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    PRINT("try lock with a count of 0", sceKernelTryLockMutex(mutex, 0));
    PRINT("cancel to a negative count", sceKernelCancelMutex(mutex, -1, &waiting));
    PRINT("cancel a non-recursive mutex to a count of 2", sceKernelCancelMutex(mutex, 2, &waiting));
    sceKernelUnlockMutex(mutex, 2);
    sceKernelUnlockMutex(mutex, 1);
    PRINT("create with attribute 0xFFFF", test_created(sceKernelCreateMutex("m", 0xFFFF, 0, NULL)));
    const SceUID owned = sceKernelCreateMutex("owned", 0, 1, NULL);
    PRINT("delete a mutex the caller owns", sceKernelDeleteMutex(owned));
    PRINT("unlock it after", sceKernelUnlockMutex(owned, 1));

    // Lightweight mutexes
    SceKernelLwMutexWork lw;
    sceKernelCreateLwMutex(&lw, "lw", 0, 0, NULL);
    PRINT("try lock a lightweight mutex with a count of 0", sceKernelTryLockLwMutex(&lw, 0));
    timeout = 0;
    PRINT("lock it with a timeout of 0 while free", sceKernelLockLwMutex(&lw, 1, &timeout));
    PRINT("delete it while the caller holds it", sceKernelDeleteLwMutex(&lw));
    PRINT("delete it again", sceKernelDeleteLwMutex(&lw));

    test_exit();
    return 0;
}
