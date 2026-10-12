// Whether a thread that holds a read lock can read lock again while a writer waits
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static SceUID rwlock;
static int write_result;

static int write_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    write_result = sceKernelLockWriteRWLock(rwlock, &timeout);
    if (write_result == 0)
        sceKernelUnlockWriteRWLock(rwlock);
    return 0;
}

int main(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    PRINT("read lock", sceKernelLockReadRWLock(rwlock, NULL));
    write_result = -1;
    const SceUID writer = test_start_thread(write_thread);
    test_wait_until_waiting(writer);
    PRINT("try read lock again while a writer waits", sceKernelTryLockReadRWLock(rwlock));
    SceUInt timeout = 100000;
    const int again = sceKernelLockReadRWLock(rwlock, &timeout);
    PRINT("read lock again while a writer waits", again);
    PRINT("the writer still waits", write_result == -1);
    if (again == 0)
        PRINT("unlock the second read lock", sceKernelUnlockReadRWLock(rwlock));
    PRINT("unlock the first read lock", sceKernelUnlockReadRWLock(rwlock));
    test_join_thread(writer);
    PRINT("the writer got the lock", write_result);

    test_exit();
    return 0;
}
