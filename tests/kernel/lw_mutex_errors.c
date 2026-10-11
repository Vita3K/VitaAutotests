// Every lightweight mutex call with a work area or UID it can't use, and with arguments it should
// reject
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceKernelLwMutexWork mutex;
static SceUID release;

// Locks the mutex and keeps it until release is signaled
static int owner_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelLockLwMutex(&mutex, 1, NULL);
    sceKernelWaitSema(release, 1, &timeout);
    sceKernelUnlockLwMutex(&mutex, 1);
    return 0;
}

int main(void) {
    // UIDs for the calls that take one
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    static SceKernelLwMutexWork deleted_work;
    sceKernelCreateLwMutex(&deleted_work, "deleted", 0, 0, NULL);
    LwMutexInfo info = { .size = sizeof(info) };
    sceKernelGetLwMutexInfo(&deleted_work, &info);
    const SceUID deleted = info.uid;
    sceKernelDeleteLwMutex(&deleted_work);
    const SceUID ids[] = { 0x12345, deleted, sema, 0 };
    const char *const names[] = { "an unknown UID", "a deleted lightweight mutex", "a semaphore", "UID 0" };
    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        info.size = sizeof(info);
        PRINTF(sceKernelGetLwMutexInfoById(ids[i], &info), "info by UID of %s", names[i]);
    }

    // Creation
    static SceKernelLwMutexWork work;
    PRINT("create with an empty name", sceKernelCreateLwMutex(&work, "", 0, 0, NULL));
    sceKernelDeleteLwMutex(&work);
    PRINT("create with attribute 0x80000000", sceKernelCreateLwMutex(&work, "m", 0x80000000, 0, NULL));
    PRINT("create twice in the same work area", sceKernelCreateLwMutex(&work, "m", 0, 0, NULL));
    sceKernelDeleteLwMutex(&work);
    PRINT("create with a negative count", sceKernelCreateLwMutex(&work, "m", MUTEX_ATTR_RECURSIVE, -1, NULL));
    PRINT("create recursive with the largest count", sceKernelCreateLwMutex(&work, "m", MUTEX_ATTR_RECURSIVE, 0x7FFFFFFF, NULL));
    sceKernelDeleteLwMutex(&work);

    // Counts on a recursive mutex this thread holds once
    sceKernelCreateLwMutex(&mutex, "mutex", MUTEX_ATTR_RECURSIVE, 1, NULL);
    PRINT("unlock 0", sceKernelUnlockLwMutex(&mutex, 0));
    PRINT("unlock -1", sceKernelUnlockLwMutex(&mutex, -1));
    PRINT("unlock 2 of 1", sceKernelUnlockLwMutex(&mutex, 2));
    SceUInt32 timeout = 1000;
    PRINT("lock with callbacks", sceKernelLockLwMutexCB(&mutex, 1, &timeout));
    PRINT("unlock all of it", sceKernelUnlockLwMutex(&mutex, 2));
    PRINT("unlock once more", sceKernelUnlockLwMutex(&mutex, 1));

    // Another thread holds it
    release = sceKernelCreateSema("release", 0, 0, 1, NULL);
    const SceUID owner = test_start_thread(owner_thread);
    test_wait_until_waiting(owner);
    PRINT("try lock a mutex another thread holds", sceKernelTryLockLwMutex(&mutex, 1));
    timeout = 1000;
    PRINT("lock it with a timeout", sceKernelLockLwMutex(&mutex, 1, &timeout));
    timeout = 1000;
    PRINT("lock it with callbacks and a timeout", sceKernelLockLwMutexCB(&mutex, 1, &timeout));
    PRINT("unlock it", sceKernelUnlockLwMutex(&mutex, 1));
    info.size = sizeof(info);
    sceKernelGetLwMutexInfo(&mutex, &info);
    PRINT("its info shows the other owner", info.currentOwnerId == owner);
    PRINT("delete it while held", sceKernelDeleteLwMutex(&mutex));
    sceKernelSignalSema(release, 1);
    test_join_thread(owner);

    // Info
    sceKernelCreateLwMutex(&work, "m", 0, 0, NULL);
    info.size = 0;
    PRINT("info with a size of 0", sceKernelGetLwMutexInfo(&work, &info));
    // With room behind the struct, in case the call writes as much as the size says
    static union {
        LwMutexInfo info;
        unsigned char bytes[sizeof(LwMutexInfo) + 64];
    } big;
    sceClibMemset(&big, 0xEE, sizeof(big));
    big.info.size = sizeof(LwMutexInfo) + 4;
    PRINT("info with a size past the struct", sceKernelGetLwMutexInfo(&work, &big.info));
    int untouched = 1;
    for (unsigned int i = sizeof(LwMutexInfo); i < sizeof(big.bytes); i++)
        untouched &= big.bytes[i] == 0xEE;
    PRINT("it leaves the bytes past the struct alone", untouched);
    PRINT("delete it", sceKernelDeleteLwMutex(&work));
    PRINT("delete it again", sceKernelDeleteLwMutex(&work));

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    static SceKernelLwMutexWork last;
    sceKernelCreateLwMutex(&last, "last", 0, 0, NULL);
    info.size = sizeof(info);
    sceKernelGetLwMutexInfo(&last, &info);
    PRINT("info with a NULL pointer", sceKernelGetLwMutexInfoById(info.uid, NULL));
    PRINT("create with a NULL work area", sceKernelCreateLwMutex(NULL, "m", 0, 0, NULL));
    PRINT("create with a NULL name", sceKernelCreateLwMutex(&work, NULL, 0, 0, NULL));

    test_exit();
    return 0;
}
