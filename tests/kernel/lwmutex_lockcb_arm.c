// The ARM coded sceKernelLockLwMutexCB, NID 6F9C4CC1 in SceLibKernel, which vitasdk has no stub for:
// a lock of a free lightweight mutex, then a lock of another free one with TLS slot 5 set to 1. The
// owner and count come from the mutex's info. The results are printed once slot 5 is back, since
// printing could lock a lightweight mutex.
//
// On a PS Vita TV on firmware 3.65 the lock of the free mutex gives 0 and the caller owns it once.
// With slot 5 set the lock fails with 0x80028004 and the mutex stays free.
#include <psp2/kernel/threadmgr.h>

#include "extra_imports.h"
#include "test.h"

// Returns whether the caller owns the mutex once
static int owned_once(SceKernelLwMutexWork *work) {
    LwMutexInfo info = { .size = sizeof(info) };
    return sceKernelGetLwMutexInfo(work, &info) == 0 && info.currentOwnerId == sceKernelGetThreadId() && info.currentCount == 1;
}

// Returns whether the mutex has no owner and no count
static int is_free(SceKernelLwMutexWork *work) {
    LwMutexInfo info = { .size = sizeof(info) };
    return sceKernelGetLwMutexInfo(work, &info) == 0 && info.currentOwnerId == 0 && info.currentCount == 0;
}

int main(void) {
    static SceKernelLwMutexWork free_lock, slot5_lock;
    sceKernelCreateLwMutex(&free_lock, "free", 0, 0, NULL);
    sceKernelCreateLwMutex(&slot5_lock, "slot5", 0, 0, NULL);

    PRINT("lock a free mutex", SceLibKernel_6F9C4CC1(&free_lock, 1, NULL));
    PRINT("the caller owns it once", owned_once(&free_lock));
    if (!is_free(&free_lock))
        sceKernelUnlockLwMutex(&free_lock, 1);

    volatile SceUInt32 *const slot = sceKernelGetTLSAddr(5);
    const SceUInt32 old = *slot;
    *slot = 1;
    const int lock = SceLibKernel_6F9C4CC1(&slot5_lock, 1, NULL);
    const int lock_free = is_free(&slot5_lock);
    *slot = old;
    if (!lock_free)
        sceKernelUnlockLwMutex(&slot5_lock, 1);
    PRINT("lock with slot 5 set to 1", lock);
    PRINT("the mutex is free after the lock", lock_free);

    sceKernelDeleteLwMutex(&free_lock);
    sceKernelDeleteLwMutex(&slot5_lock);
    test_exit();
    return 0;
}
