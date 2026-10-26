// A recursive reader/writer lock that the main thread write locks twice, with a reader R and then a
// writer W queued behind it. The main thread unlocks the write lock once, which leaves it held, and
// then once more, which hands it to W. W holds the lock until the gate is signaled, then unlocks
// it, which hands it to R, and R holds it until the gate is signaled again. After each step the
// test prints the wait types of R and W, the lock count, whether the main thread is the write owner
// and the waiting reader and writer counts. It also prints each lock and unlock result, and the
// results of R and W at the end. The lock has no priority attribute, so the queue is in arrival
// order. The model rwlock_wake_order covers the hand off order of unlocks with waiters, and
// rwlock_counts covers the counts of a recursive lock.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID rwlock, gate, main_thread, reader_uid, writer_uid;
static volatile int reader_result = 0xDEAD, reader_unlock = 0xDEAD;
static volatile int writer_result = 0xDEAD, writer_unlock = 0xDEAD;

// Takes the read lock, holds it until the gate is signaled, then gives it back
static int reader_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    reader_result = sceKernelLockReadRWLock(rwlock, &timeout);
    if (reader_result == 0) {
        timeout = TEST_WAIT_US;
        sceKernelWaitSema(gate, 1, &timeout);
        reader_unlock = sceKernelUnlockReadRWLock(rwlock);
    }
    return 0;
}

// Takes the write lock, holds it until the gate is signaled, then gives it back
static int writer_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    writer_result = sceKernelLockWriteRWLock(rwlock, &timeout);
    if (writer_result == 0) {
        timeout = TEST_WAIT_US;
        sceKernelWaitSema(gate, 1, &timeout);
        writer_unlock = sceKernelUnlockWriteRWLock(rwlock);
    }
    return 0;
}

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

// Prints the wait types of R and W, then the lock's count, write owner and waiter counts
static void print_state(const char *step) {
    SceKernelRWLockInfo info = { .size = sizeof(info) };
    sceKernelGetRWLockInfo(rwlock, &info);
    PRINTF(wait_type_of(reader_uid), "%s: the reader's wait type", step);
    PRINTF(wait_type_of(writer_uid), "%s: the writer's wait type", step);
    PRINTF(info.lockCount, "%s: the lock count", step);
    PRINTF(info.writeOwnerId == main_thread, "%s: the main thread is the write owner", step);
    PRINTF(info.numReadWaitThreads, "%s: the waiting readers", step);
    PRINTF(info.numWriteWaitThreads, "%s: the waiting writers", step);
}

int main(void) {
    main_thread = sceKernelGetThreadId();
    rwlock = sceKernelCreateRWLock("rwlock", RW_LOCK_ATTR_RECURSIVE, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    SceUInt32 lock_timeout = TEST_WAIT_US;
    PRINT("first write lock", sceKernelLockWriteRWLock(rwlock, &lock_timeout));
    lock_timeout = TEST_WAIT_US;
    PRINT("second write lock", sceKernelLockWriteRWLock(rwlock, &lock_timeout));

    reader_uid = test_start_thread(reader_entry);
    TEST_WAIT_FOR(wait_type_of(reader_uid) == WAIT_TYPE_RW_LOCK);
    writer_uid = test_start_thread(writer_entry);
    TEST_WAIT_FOR(wait_type_of(writer_uid) == WAIT_TYPE_RW_LOCK);
    print_state("both queued");

    PRINT("first write unlock", sceKernelUnlockWriteRWLock(rwlock));
    print_state("after the first unlock");

    // The lock goes to the waiting writer, which holds it at the gate
    PRINT("second write unlock", sceKernelUnlockWriteRWLock(rwlock));
    TEST_WAIT_FOR(wait_type_of(writer_uid) == WAIT_TYPE_SEMA);
    print_state("after the second unlock");

    sceKernelSignalSema(gate, 1);
    test_join_thread(writer_uid);
    PRINT("W's result", writer_result);
    PRINT("W's unlock", writer_unlock);

    // The reader gets the lock once W unlocks it, and holds it at the gate
    TEST_WAIT_FOR(wait_type_of(reader_uid) == WAIT_TYPE_SEMA);
    print_state("after W's unlock");

    sceKernelSignalSema(gate, 1);
    test_join_thread(reader_uid);
    PRINT("R's result", reader_result);
    PRINT("R's unlock", reader_unlock);

    sceKernelDeleteRWLock(rwlock);
    sceKernelDeleteSema(gate);
    test_exit();
    return 0;
}
