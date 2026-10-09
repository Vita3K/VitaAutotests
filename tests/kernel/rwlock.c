#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static SceUID rwlock;
static int try_read_result, try_write_result;
static int lock_result;

static int try_lock_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    try_read_result = sceKernelTryLockReadRWLock(rwlock);
    if (try_read_result == 0)
        sceKernelUnlockReadRWLock(rwlock);
    try_write_result = sceKernelTryLockWriteRWLock(rwlock);
    if (try_write_result == 0)
        sceKernelUnlockWriteRWLock(rwlock);
    return 0;
}

// Waits for the write lock, with a timeout so the test ends even if a check fails
static int write_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    unsigned int timeout = 500000;
    lock_result = sceKernelLockWriteRWLock(rwlock, &timeout);
    if (lock_result == 0)
        sceKernelUnlockWriteRWLock(rwlock);
    return 0;
}

static int read_results[2], other_unlock;

// Waits for a read lock into slot i, and unlocks it again if it got it
static int read_slot_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    unsigned int timeout = TEST_WAIT_US;
    read_results[i] = sceKernelLockReadRWLock(rwlock, &timeout);
    if (read_results[i] == 0)
        sceKernelUnlockReadRWLock(rwlock);
    return 0;
}

static int unlock_write_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    other_unlock = sceKernelUnlockWriteRWLock(rwlock);
    return 0;
}

static int read_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    unsigned int timeout = 500000;
    lock_result = sceKernelLockReadRWLock(rwlock, &timeout);
    return 0;
}

int main(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);

    PRINT("try read lock on a free lock", sceKernelTryLockReadRWLock(rwlock));
    PRINT("read lock again on the same thread", sceKernelLockReadRWLock(rwlock, NULL));
    test_join_thread(test_start_thread(try_lock_thread));
    PRINT("another thread can read lock too", try_read_result);
    PRINT("no thread can write lock while read locked", try_write_result);
    PRINT("unlock the first read lock", sceKernelUnlockReadRWLock(rwlock));
    PRINT("unlock the second read lock", sceKernelUnlockReadRWLock(rwlock));
    PRINT("unlock a read lock nobody holds", sceKernelUnlockReadRWLock(rwlock));

    PRINT("try write lock on a free lock", sceKernelTryLockWriteRWLock(rwlock));
    test_join_thread(test_start_thread(try_lock_thread));
    PRINT("try read lock on a write locked lock", try_read_result);
    PRINT("try write lock on a write locked lock", try_write_result);
    sceKernelUnlockWriteRWLock(rwlock);

    // A waiting writer keeps new readers out, and gets the lock when the last reader leaves
    sceKernelLockReadRWLock(rwlock, NULL);
    lock_result = -1;
    const SceUID writer = test_start_thread(write_thread);
    test_wait_until_waiting(writer);
    test_join_thread(test_start_thread(try_lock_thread));
    PRINT("a new reader while a writer waits", try_read_result);
    sceKernelUnlockReadRWLock(rwlock);
    test_join_thread(writer);
    PRINT("the last reader leaving ends the writer's wait", lock_result);

    // Only the writer can unlock the write lock, and its unlock wakes every waiting reader
    sceKernelLockWriteRWLock(rwlock, NULL);
    test_join_thread(test_start_thread(unlock_write_thread));
    PRINT("a write unlock by a thread that isn't the writer", other_unlock < 0);
    read_results[0] = read_results[1] = -1;
    const SceUID first = test_start_thread_arg(read_slot_thread, 0);
    test_wait_until_waiting(first);
    const SceUID second = test_start_thread_arg(read_slot_thread, 1);
    test_wait_until_waiting(second);
    sceKernelUnlockWriteRWLock(rwlock);
    test_join_thread(first);
    test_join_thread(second);
    PRINT("the write unlock wakes the first waiting reader", read_results[0]);
    PRINT("and the second", read_results[1]);

    sceKernelLockWriteRWLock(rwlock, NULL);
    lock_result = -1;
    const SceUID reader = test_start_thread(read_thread);
    test_wait_until_waiting(reader);
    PRINT("delete", sceKernelDeleteRWLock(rwlock));
    test_join_thread(reader);
    PRINT("a wait on a deleted lock", lock_result);

    test_exit();
    return 0;
}
