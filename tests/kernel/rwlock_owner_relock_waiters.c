// The write owner's own lock and try lock calls on a reader/writer lock while a reader R and a
// writer W wait for it. The main thread write locks the lock, then R queues for a read lock and W
// for the write lock, in that order. The lock has no priority attribute, so the queue is in arrival
// order. After each step the test prints the lock count, the write owner (0 for none, 1 for the
// main thread, 2 for another thread), the waiting reader and writer counts, the wait types of R and
// W, and the result of each call. The cases:
// - a recursive lock. The main thread write locks it with a timeout of 0, which adds to the count
//   at once, then try write locks it, then read locks it with a timeout of 0 and try read locks it,
//   which fail with 0x800281E6. Then it unlocks the write lock twice, which should leave the count
//   above 0 and wake nobody, and once more, which hands the lock to W while R still waits.
// - a plain lock. The write lock with a timeout of 0 and the try write lock fail with 0x800281E2,
//   the read lock with a timeout of 0 and the try read lock fail with 0x800281E6, and a read unlock
//   fails with 0x800281E7. The count stays 1 and R and W stay queued. Then the owner's write lock
//   and read lock are called with a timeout variable of 500 ms each, and the test prints whether
//   each timeout variable is unchanged. Then the write unlock hands the lock to W.
// After each hand over, W holds the lock at a gate until the test lets it go, and R holds it the
// same way after W unlocks. Every thread is pinned to core 0, and R and W run below the main
// thread.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0

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

// Creates a thread on core 0 below the main thread and starts it
static SceUID start_low_thread(const char *name, SceKernelThreadEntry entry) {
    const SceUID thread =
        sceKernelCreateThread(name, entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

// Codes the write owner: 0 for none, 1 for the main thread and 2 for another thread
static int owner_code(SceUID owner) {
    return owner == 0 ? 0 : owner == main_thread ? 1 : 2;
}

// Prints the count, owner, waiter counts and both wait types after a step
static void print_state(const char *step) {
    SceKernelRWLockInfo info = { .size = sizeof(info) };
    sceKernelGetRWLockInfo(rwlock, &info);
    PRINTF(info.lockCount, "%s: the lock count", step);
    PRINTF(owner_code(info.writeOwnerId), "%s: the write owner", step);
    PRINTF(info.numReadWaitThreads, "%s: the waiting readers", step);
    PRINTF(info.numWriteWaitThreads, "%s: the waiting writers", step);
    PRINTF(wait_type_of(reader_uid), "%s: R's wait type", step);
    PRINTF(wait_type_of(writer_uid), "%s: W's wait type", step);
}

// Creates the lock with attr, write locks it for the main thread, then queues R and W behind it
static void queue_waiters(SceUInt attr, const char *label) {
    rwlock = sceKernelCreateRWLock("rwlock", attr, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    reader_result = reader_unlock = writer_result = writer_unlock = 0xDEAD;
    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelLockWriteRWLock(rwlock, &timeout), "%s: the first write lock", label);
    reader_uid = start_low_thread("reader", reader_entry);
    TEST_WAIT_FOR(wait_type_of(reader_uid) == WAIT_TYPE_RW_LOCK);
    writer_uid = start_low_thread("writer", writer_entry);
    TEST_WAIT_FOR(wait_type_of(writer_uid) == WAIT_TYPE_RW_LOCK);
}

// Waits until W owns the lock and holds it at the gate, so its state can be read while blocked
static void wait_for_writer(void) {
    TEST_WAIT_FOR(wait_type_of(writer_uid) == WAIT_TYPE_SEMA);
}

// Lets W go on from the gate, prints its results, then waits until R holds the lock at the gate
static void release_writer(const char *label) {
    sceKernelSignalSema(gate, 1);
    test_join_thread(writer_uid);
    PRINTF(writer_result, "%s: W's lock", label);
    PRINTF(writer_unlock, "%s: W's unlock", label);
    TEST_WAIT_FOR(wait_type_of(reader_uid) == WAIT_TYPE_SEMA);
}

// Lets R go on from the gate, prints its results, then deletes the objects
static void release_reader(const char *label) {
    sceKernelSignalSema(gate, 1);
    test_join_thread(reader_uid);
    PRINTF(reader_result, "%s: R's lock", label);
    PRINTF(reader_unlock, "%s: R's unlock", label);
    sceKernelDeleteRWLock(rwlock);
    sceKernelDeleteSema(gate);
}

static void recursive_case(void) {
    queue_waiters(RW_LOCK_ATTR_RECURSIVE, "recursive");
    print_state("recursive: both queued");
    SceUInt32 timeout = 0;
    PRINT("recursive: write lock with a timeout of 0", sceKernelLockWriteRWLock(rwlock, &timeout));
    print_state("recursive: after the write lock");
    PRINT("recursive: try write lock", sceKernelTryLockWriteRWLock(rwlock));
    print_state("recursive: after the try write lock");
    timeout = 0;
    PRINT("recursive: read lock with a timeout of 0", sceKernelLockReadRWLock(rwlock, &timeout));
    PRINT("recursive: try read lock", sceKernelTryLockReadRWLock(rwlock));
    print_state("recursive: after the read locks");
    PRINT("recursive: first write unlock", sceKernelUnlockWriteRWLock(rwlock));
    print_state("recursive: after the first write unlock");
    PRINT("recursive: second write unlock", sceKernelUnlockWriteRWLock(rwlock));
    print_state("recursive: after the second write unlock");
    PRINT("recursive: third write unlock", sceKernelUnlockWriteRWLock(rwlock));
    wait_for_writer();
    print_state("recursive: after the third write unlock");
    release_writer("recursive");
    print_state("recursive: after W's unlock");
    release_reader("recursive");
}

static void plain_case(void) {
    queue_waiters(0, "plain");
    print_state("plain: both queued");
    SceUInt32 timeout = 0;
    PRINT("plain: write lock with a timeout of 0", sceKernelLockWriteRWLock(rwlock, &timeout));
    PRINT("plain: try write lock", sceKernelTryLockWriteRWLock(rwlock));
    timeout = 0;
    PRINT("plain: read lock with a timeout of 0", sceKernelLockReadRWLock(rwlock, &timeout));
    PRINT("plain: read unlock", sceKernelUnlockReadRWLock(rwlock));
    print_state("plain: after the owner's calls");
    timeout = 500000;
    PRINT("plain: write lock with a timeout of 500 ms", sceKernelLockWriteRWLock(rwlock, &timeout));
    PRINT("plain: the timeout after the write lock is unchanged", timeout == 500000);
    timeout = 500000;
    PRINT("plain: read lock with a timeout of 500 ms", sceKernelLockReadRWLock(rwlock, &timeout));
    PRINT("plain: the timeout after the read lock is unchanged", timeout == 500000);
    print_state("plain: after the timeouts");
    PRINT("plain: write unlock", sceKernelUnlockWriteRWLock(rwlock));
    wait_for_writer();
    print_state("plain: after the write unlock");
    release_writer("plain");
    print_state("plain: after W's unlock");
    release_reader("plain");
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);
    main_thread = sceKernelGetThreadId();

    recursive_case();
    plain_case();

    test_exit();
    return 0;
}
