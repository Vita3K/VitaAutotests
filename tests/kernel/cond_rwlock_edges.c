// Condition variable and reader/writer lock edge cases
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID mutex, cond, rwlock;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static int wait_result, other_result;

// Waits on the condition, with the mutex locked as it should be
static int cond_wait_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    SceUInt timeout = TEST_WAIT_US;
    wait_result = sceKernelWaitCond(cond, &timeout);
    return 0;
}

static int lw_cond_wait_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockLwMutex(&lw_mutex, 1, NULL);
    unsigned int timeout = TEST_WAIT_US;
    wait_result = sceKernelWaitLwCond(&lw_cond, &timeout);
    return 0;
}

static int unlock_write_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    other_result = sceKernelUnlockWriteRWLock(rwlock);
    return 0;
}

int main(void) {
    SceUInt timeout;

    // Waiting without holding the mutex
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    timeout = 1000;
    PRINT("wait on a condition without holding its mutex", sceKernelWaitCond(cond, &timeout));
    sceKernelCreateLwMutex(&lw_mutex, "lw_mutex", 0, 0, NULL);
    sceKernelCreateLwCond(&lw_cond, "lw_cond", 0, &lw_mutex, NULL);
    unsigned int lw_timeout = 1000;
    PRINT("wait on a lightweight condition without holding its mutex", sceKernelWaitLwCond(&lw_cond, &lw_timeout));

    // Signaling threads that don't wait
    PRINT("signal a thread that isn't waiting", sceKernelSignalCondTo(cond, sceKernelGetThreadId()));
    PRINT("signal an unknown thread", sceKernelSignalCondTo(cond, 0x12345));
    PRINT("create on an unknown mutex", test_created(sceKernelCreateCond("bad", 0, 0x12345, NULL)));

    // Deleting the mutex under a waiting condition
    wait_result = -1;
    SceUID waiter = test_start_thread(cond_wait_thread);
    test_wait_until_waiting(waiter);
    PRINT("delete the mutex of a waiting condition", sceKernelDeleteMutex(mutex));
    test_join_thread(waiter);
    PRINT("the condition wait after its mutex is deleted", wait_result);

    wait_result = -1;
    waiter = test_start_thread(lw_cond_wait_thread);
    test_wait_until_waiting(waiter);
    PRINT("delete the lightweight mutex of a waiting condition", sceKernelDeleteLwMutex(&lw_mutex));
    test_join_thread(waiter);
    PRINT("the lightweight condition wait after its mutex is deleted", wait_result);

    // Reader/writer locks taken again by the same thread
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    PRINT("write lock", sceKernelTryLockWriteRWLock(rwlock));
    PRINT("write lock again without the recursive attribute", sceKernelTryLockWriteRWLock(rwlock));
    PRINT("read lock while holding the write lock", sceKernelTryLockReadRWLock(rwlock));
    PRINT("read unlock while holding the write lock", sceKernelUnlockReadRWLock(rwlock));
    test_join_thread(test_start_thread(unlock_write_thread));
    PRINT("write unlock by a thread that isn't the writer", other_result);
    PRINT("write unlock", sceKernelUnlockWriteRWLock(rwlock));
    PRINT("write unlock while not locked", sceKernelUnlockWriteRWLock(rwlock));
    PRINT("read lock", sceKernelTryLockReadRWLock(rwlock));
    PRINT("write lock while holding a read lock", sceKernelTryLockWriteRWLock(rwlock));
    PRINT("read unlock", sceKernelUnlockReadRWLock(rwlock));
    PRINT("create with attribute 0x2", test_created(sceKernelCreateRWLock("recursive", 0x2, NULL)));
    PRINT("create with attribute 0xFFFF", test_created(sceKernelCreateRWLock("attrs", 0xFFFF, NULL)));

    test_exit();
    return 0;
}
