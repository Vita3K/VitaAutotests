// Every lightweight condition variable call with a work area or UID it can't use, and with arguments
// it should reject
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    static SceKernelLwMutexWork mutex;
    sceKernelCreateLwMutex(&mutex, "mutex", 0, 0, NULL);

    // UIDs for the calls that take one
    static SceKernelLwCondWork deleted_work;
    sceKernelCreateLwCond(&deleted_work, "deleted", 0, &mutex, NULL);
    LwCondInfo info = { .size = sizeof(info) };
    sceKernelGetLwCondInfo(&deleted_work, &info);
    const SceUID deleted = info.uid;
    sceKernelDeleteLwCond(&deleted_work);
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    const SceUID ids[] = { 0x12345, deleted, sema, 0 };
    const char *const names[] = { "an unknown UID", "a deleted lightweight condition variable", "a semaphore", "UID 0" };
    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        info.size = sizeof(info);
        PRINTF(sceKernelGetLwCondInfoById(ids[i], &info), "info by UID of %s", names[i]);
    }

    // Creation
    static SceKernelLwCondWork work;
    PRINT("create with an empty name", sceKernelCreateLwCond(&work, "", 0, &mutex, NULL));
    sceKernelDeleteLwCond(&work);
    PRINT("create with attribute 0x80000000", sceKernelCreateLwCond(&work, "c", 0x80000000, &mutex, NULL));
    sceKernelDeleteLwCond(&work);
    static SceKernelLwMutexWork deleted_mutex;
    sceKernelCreateLwMutex(&deleted_mutex, "deleted", 0, 0, NULL);
    sceKernelDeleteLwMutex(&deleted_mutex);
    PRINT("create with a deleted mutex", sceKernelCreateLwCond(&work, "c", 0, &deleted_mutex, NULL));
    sceKernelDeleteLwCond(&work);

    // Signals with nobody waiting, to threads that can't be woken
    static SceKernelLwCondWork cond;
    sceKernelCreateLwCond(&cond, "cond", 0, &mutex, NULL);
    PRINT("signal with nobody waiting", sceKernelSignalLwCond(&cond));
    PRINT("signal all with nobody waiting", sceKernelSignalLwCondAll(&cond));
    PRINT("signal to this thread", sceKernelSignalLwCondTo(&cond, sceKernelGetThreadId()));
    PRINT("signal to an unknown thread", sceKernelSignalLwCondTo(&cond, 0x12345));
    PRINT("signal to thread 0", sceKernelSignalLwCondTo(&cond, 0));
    PRINT("signal to a semaphore", sceKernelSignalLwCondTo(&cond, sema));
    SceUInt32 timeout = 0;
    sceKernelLockLwMutex(&mutex, 1, NULL);
    PRINT("wait with a timeout of 0", sceKernelWaitLwCond(&cond, &timeout));
    PRINT("the mutex is held again after it", sceKernelUnlockLwMutex(&mutex, 1));

    // Deleting
    PRINT("delete the mutex while a condition variable uses it", sceKernelDeleteLwMutex(&mutex));
    PRINT("signal after it", sceKernelSignalLwCond(&cond));
    PRINT("delete it", sceKernelDeleteLwCond(&cond));
    PRINT("delete it again", sceKernelDeleteLwCond(&cond));
    PRINT("signal a deleted one", sceKernelSignalLwCond(&cond));

    // Info
    sceKernelCreateLwMutex(&mutex, "mutex", 0, 0, NULL);
    sceKernelCreateLwCond(&cond, "cond", 0, &mutex, NULL);
    info.size = 0;
    PRINT("info with a size of 0", sceKernelGetLwCondInfo(&cond, &info));
    // With room behind the struct, in case the call writes as much as the size says
    static union {
        LwCondInfo info;
        unsigned char bytes[sizeof(LwCondInfo) + 64];
    } big;
    sceClibMemset(&big, 0xEE, sizeof(big));
    big.info.size = sizeof(LwCondInfo) + 4;
    PRINT("info with a size past the struct", sceKernelGetLwCondInfo(&cond, &big.info));
    int untouched = 1;
    for (unsigned int i = sizeof(LwCondInfo); i < sizeof(big.bytes); i++)
        untouched &= big.bytes[i] == 0xEE;
    PRINT("it leaves the bytes past the struct alone", untouched);

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    info.size = sizeof(info);
    sceKernelGetLwCondInfo(&cond, &info);
    PRINT("info with a NULL pointer", sceKernelGetLwCondInfoById(info.uid, NULL));
    PRINT("create with a NULL mutex", sceKernelCreateLwCond(&work, "c", 0, NULL, NULL));
    PRINT("create with a NULL work area", sceKernelCreateLwCond(NULL, "c", 0, &mutex, NULL));
    PRINT("create with a NULL name", sceKernelCreateLwCond(&work, NULL, 0, &mutex, NULL));

    test_exit();
    return 0;
}
