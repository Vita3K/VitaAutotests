// Every reader/writer lock call with a UID that isn't a reader/writer lock it can use, and with
// arguments it should reject
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID rwlock;
static int wait_result;

static int write_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    wait_result = sceKernelLockWriteRWLock(rwlock, &timeout);
    if (wait_result == 0)
        sceKernelUnlockWriteRWLock(rwlock);
    return 0;
}

int main(void) {
    const SceUID deleted = sceKernelCreateRWLock("deleted", 0, NULL);
    sceKernelDeleteRWLock(deleted);
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    const SceUID ids[] = { 0x12345, deleted, sema, 0, -1 };
    const char *const names[] = { "an unknown UID", "a deleted reader/writer lock", "a semaphore", "UID 0", "UID -1" };

    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        SceUInt32 timeout = 1000;
        SceUInt32 readers = 0xDEAD, writers = 0xDEAD;
        SceKernelRWLockInfo info = { .size = sizeof(info) };
        PRINTF(sceKernelLockReadRWLock(ids[i], &timeout), "read lock %s", names[i]);
        timeout = 1000;
        PRINTF(sceKernelLockReadRWLockCB(ids[i], &timeout), "read lock with callbacks %s", names[i]);
        PRINTF(sceKernelTryLockReadRWLock(ids[i]), "try read lock %s", names[i]);
        PRINTF(sceKernelUnlockReadRWLock(ids[i]), "read unlock %s", names[i]);
        timeout = 1000;
        PRINTF(sceKernelLockWriteRWLock(ids[i], &timeout), "write lock %s", names[i]);
        timeout = 1000;
        PRINTF(sceKernelLockWriteRWLockCB(ids[i], &timeout), "write lock with callbacks %s", names[i]);
        PRINTF(sceKernelTryLockWriteRWLock(ids[i]), "try write lock %s", names[i]);
        PRINTF(sceKernelUnlockWriteRWLock(ids[i]), "write unlock %s", names[i]);
        PRINTF(sceKernelCancelRWLock(ids[i], &readers, &writers, 0), "cancel %s", names[i]);
        PRINTF(sceKernelGetRWLockInfo(ids[i], &info), "info of %s", names[i]);
        PRINTF(sceKernelCloseRWLock(ids[i]), "close %s", names[i]);
        PRINTF(sceKernelDeleteRWLock(ids[i]), "delete %s", names[i]);
    }
    PRINT("the semaphore survives the reader/writer lock calls", sceKernelPollSema(sema, 1));

    // Creation
    PRINT("create with an empty name", test_created(sceKernelCreateRWLock("", 0, NULL)));
    PRINT("create with attribute 0x80000000", test_created(sceKernelCreateRWLock("r", 0x80000000, NULL)));
    PRINT("create with the recursive attribute", test_created(sceKernelCreateRWLock("r", MUTEX_ATTR_RECURSIVE, NULL)));

    // Read locks taken more than once by one thread
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    PRINT("read lock", sceKernelTryLockReadRWLock(rwlock));
    PRINT("read lock again", sceKernelTryLockReadRWLock(rwlock));
    PRINT("read unlock", sceKernelUnlockReadRWLock(rwlock));
    PRINT("read unlock again", sceKernelUnlockReadRWLock(rwlock));
    PRINT("read unlock once more", sceKernelUnlockReadRWLock(rwlock));

    // Cancel with a waiting writer
    sceKernelTryLockReadRWLock(rwlock);
    const SceUID waiter = test_start_thread(write_waiter);
    test_wait_until_waiting(waiter);
    SceUInt32 readers = 0xDEAD, writers = 0xDEAD;
    PRINT("cancel with a waiting writer", sceKernelCancelRWLock(rwlock, &readers, &writers, 0));
    test_join_thread(waiter);
    PRINT("the waiting readers", readers);
    PRINT("the waiting writers", writers);
    PRINT("the writer's result", wait_result);
    PRINT("read unlock after the cancel", sceKernelUnlockReadRWLock(rwlock));
    PRINT("cancel with flag 1", sceKernelCancelRWLock(rwlock, NULL, NULL, 1));
    PRINT("write unlock after it", sceKernelUnlockWriteRWLock(rwlock));
    PRINT("cancel with flag 2", sceKernelCancelRWLock(rwlock, NULL, NULL, 2));
    PRINT("cancel with flag 3", sceKernelCancelRWLock(rwlock, NULL, NULL, 3));
    PRINT("try write lock after the cancels", sceKernelTryLockWriteRWLock(rwlock));
    PRINT("delete it while write locked", sceKernelDeleteRWLock(rwlock));

    // Info
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    SceKernelRWLockInfo info = { .size = 0 };
    PRINT("info with a size of 0", sceKernelGetRWLockInfo(rwlock, &info));
    // With room behind the struct, in case the call writes as much as the size says
    static union {
        SceKernelRWLockInfo info;
        unsigned char bytes[sizeof(SceKernelRWLockInfo) + 64];
    } big;
    sceClibMemset(&big, 0xEE, sizeof(big));
    big.info.size = sizeof(SceKernelRWLockInfo) + 4;
    PRINT("info with a size past the struct", sceKernelGetRWLockInfo(rwlock, &big.info));
    int untouched = 1;
    for (unsigned int i = sizeof(SceKernelRWLockInfo); i < sizeof(big.bytes); i++)
        untouched &= big.bytes[i] == 0xEE;
    PRINT("it leaves the bytes past the struct alone", untouched);

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("info with a NULL pointer", sceKernelGetRWLockInfo(rwlock, NULL));
    PRINT("open with a NULL name", sceKernelOpenRWLock(NULL));
    PRINT("create with a NULL name", test_created(sceKernelCreateRWLock(NULL, 0, NULL)));

    test_exit();
    return 0;
}
