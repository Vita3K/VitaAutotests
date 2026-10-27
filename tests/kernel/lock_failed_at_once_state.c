// What a lock leaves behind when it fails at once. A lock with a timeout of 0 or 1 microsecond that
// can't be taken fails without waiting, so its waiter bookkeeping must be undone. Each case has the
// main thread hold the lock it needs, makes a lock call that must fail at once from another thread,
// and then checks that nothing from that call stays: a reader or writer that waits still gets the
// lock, and the waiter counts, owner and later lock calls show nothing queued. Every case runs with
// a timeout of 0 and of 1 microsecond, and with the plain and the callback form of the call. The
// cases:
// - a: main holds the write lock and a write lock fails at once. Then a reader waits, and main's
//   unlock must give it the lock.
// - b: main holds a read lock and a write lock fails at once. Then a try read and a read lock with
//   timeout 0 must both work, the waiter counts must be 0, and three read unlocks leave the count
//   at 0.
// - c: main holds a read lock and a writer waits. A read lock fails at once behind the writer. Then
//   main's read unlock gives the lock to the writer, which keeps it until main read the owner, and
//   after the writer's unlock nothing is left queued or held.
// - d: main holds a mutex and a lock on it fails at once. Then main unlocks, the mutex is free for
//   a third thread's try lock, and no thread waits for it.
//
// The test prints each failing call's result, the waiter counts and owner codes around it, and the
// results of the calls that must work, after every thread of the case has ended.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define LOW_PRIORITY 0xB0

// The kinds of lock call a case can make. A failing call takes one of these.
enum Kind { WRITE_LOCK, READ_LOCK, MUTEX_LOCK };

static SceUID rwlock, mutex, main_thread;

// What the failing call does, set before its thread starts
static int fail_kind, fail_cb;
static SceUInt fail_timeout;
static volatile int fail_result, waiter_result, try_result;
// Set once main read who owns the write lock, which the writer keeps until then
static volatile int writer_may_unlock;

// Makes the lock call of kind, plain or with callbacks, with timeout, and returns its result
static int lock_once(int kind, int cb, SceUInt *timeout) {
    switch (kind) {
    case WRITE_LOCK:
        if (cb)
            return sceKernelLockWriteRWLockCB(rwlock, timeout);
        return sceKernelLockWriteRWLock(rwlock, timeout);
    case READ_LOCK:
        if (cb)
            return sceKernelLockReadRWLockCB(rwlock, timeout);
        return sceKernelLockReadRWLock(rwlock, timeout);
    case MUTEX_LOCK:
        if (cb)
            return sceKernelLockMutexCB(mutex, 1, timeout);
        return sceKernelLockMutex(mutex, 1, timeout);
    }
    return 0;
}

// Makes the lock call that must fail at once, and keeps its result
static int failing_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = fail_timeout;
    fail_result = lock_once(fail_kind, fail_cb, &timeout);
    return 0;
}

// Waits a long time for the write lock, and if it gets it, unlocks it again once main read the owner
static int write_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    waiter_result = sceKernelLockWriteRWLock(rwlock, &timeout);
    if (waiter_result == 0) {
        TEST_WAIT_FOR(writer_may_unlock);
        sceKernelUnlockWriteRWLock(rwlock);
    }
    return 0;
}

// Waits a long time for a read lock, and unlocks it again if it gets it
static int read_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    waiter_result = sceKernelLockReadRWLock(rwlock, &timeout);
    if (waiter_result == 0)
        sceKernelUnlockReadRWLock(rwlock);
    return 0;
}

// Tries the mutex, and unlocks it again if it gets it
static int try_mutex(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    try_result = sceKernelTryLockMutex(mutex, 1);
    if (try_result == 0)
        sceKernelUnlockMutex(mutex, 1);
    return 0;
}

// Whether thread waits for a reader/writer lock, with or without callbacks
static int waits_for_rwlock(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && (info.status & SCE_THREAD_WAITING) &&
           (info.waitType & ~WAIT_TYPE_CB) == WAIT_TYPE_RW_LOCK;
}

// Starts entry on a new thread at priority, on core 0
static SceUID start_thread(SceKernelThreadEntry entry, int priority) {
    const SceUID thread = sceKernelCreateThread(
        "test", entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

// 0 for no owner, 1 for the main thread, 2 for any other thread
static int owner_code(SceUID owner) {
    if (owner == 0)
        return 0;
    return owner == main_thread ? 1 : 2;
}

// Writes the label of a case run into tag, like "a, timeout 0, plain"
static void make_tag(char *tag, SceSize size, char letter, SceUInt timeout, int cb) {
    sceClibSnprintf(tag, size, "%c, timeout %u, %s", letter, (unsigned)timeout,
                    cb ? "with callbacks" : "plain");
}

// Main holds the write lock, a write lock fails at once, and a reader waits behind main. Main's
// unlock must give the reader the lock.
static void case_a(SceUInt timeout, int cb) {
    char tag[64];
    make_tag(tag, sizeof(tag), 'a', timeout, cb);
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    SceUInt wait = TEST_WAIT_US;
    sceKernelLockWriteRWLock(rwlock, &wait);
    fail_kind = WRITE_LOCK;
    fail_cb = cb;
    fail_timeout = timeout;
    test_join_thread(start_thread(failing_entry, LOW_PRIORITY));
    const int failed = fail_result;

    const SceUID reader = start_thread(read_waiter, LOW_PRIORITY);
    TEST_WAIT_FOR(waits_for_rwlock(reader));
    SceKernelRWLockInfo info = { .size = sizeof(info) };
    sceKernelGetRWLockInfo(rwlock, &info);
    const int readers = info.numReadWaitThreads;
    const int writers = info.numWriteWaitThreads;
    const int owner = owner_code(info.writeOwnerId);

    sceKernelUnlockWriteRWLock(rwlock);
    test_join_thread(reader);
    const int reader_result = waiter_result;
    sceKernelGetRWLockInfo(rwlock, &info);
    const int count_after = info.lockCount;

    PRINTF(failed, "%s: the write lock that fails at once", tag);
    PRINTF(readers, "%s: waiting readers before the unlock", tag);
    PRINTF(writers, "%s: waiting writers before the unlock", tag);
    PRINTF(owner, "%s: write owner before the unlock (0 none, 1 main, 2 other)", tag);
    PRINTF(reader_result, "%s: the reader's lock", tag);
    PRINTF(count_after, "%s: lock count after the reader's unlock", tag);
    sceKernelDeleteRWLock(rwlock);
}

// Main holds a read lock and a write lock fails at once. Nothing may be queued afterwards: a try
// read and a read lock with timeout 0 must work, the waiter counts must be 0, and three read
// unlocks must leave the count at 0.
static void case_b(SceUInt timeout, int cb) {
    char tag[64];
    make_tag(tag, sizeof(tag), 'b', timeout, cb);
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    SceUInt wait = TEST_WAIT_US;
    sceKernelLockReadRWLock(rwlock, &wait);
    fail_kind = WRITE_LOCK;
    fail_cb = cb;
    fail_timeout = timeout;
    test_join_thread(start_thread(failing_entry, LOW_PRIORITY));
    const int failed = fail_result;

    SceKernelRWLockInfo info = { .size = sizeof(info) };
    sceKernelGetRWLockInfo(rwlock, &info);
    const int readers = info.numReadWaitThreads;
    const int writers = info.numWriteWaitThreads;
    const int count_before = info.lockCount;
    const int owner = owner_code(info.writeOwnerId);

    const int try_read = sceKernelTryLockReadRWLock(rwlock);
    SceUInt zero = 0;
    const int read_lock = sceKernelLockReadRWLock(rwlock, &zero);
    sceKernelGetRWLockInfo(rwlock, &info);
    const int count_locked = info.lockCount;
    const int queued_readers = info.numReadWaitThreads;
    const int queued_writers = info.numWriteWaitThreads;

    const int unlock_1 = sceKernelUnlockReadRWLock(rwlock);
    const int unlock_2 = sceKernelUnlockReadRWLock(rwlock);
    const int unlock_3 = sceKernelUnlockReadRWLock(rwlock);
    sceKernelGetRWLockInfo(rwlock, &info);
    const int count_after = info.lockCount;

    PRINTF(failed, "%s: the write lock that fails at once", tag);
    PRINTF(readers, "%s: waiting readers after it", tag);
    PRINTF(writers, "%s: waiting writers after it", tag);
    PRINTF(owner, "%s: write owner after it (0 none, 1 main, 2 other)", tag);
    PRINTF(try_read, "%s: try read lock", tag);
    PRINTF(read_lock, "%s: read lock with timeout 0", tag);
    PRINTF(count_before, "%s: lock count before the two read locks", tag);
    PRINTF(count_locked, "%s: lock count after the two read locks", tag);
    PRINTF(queued_readers, "%s: waiting readers after the two read locks", tag);
    PRINTF(queued_writers, "%s: waiting writers after the two read locks", tag);
    PRINTF(unlock_1, "%s: read unlock 1", tag);
    PRINTF(unlock_2, "%s: read unlock 2", tag);
    PRINTF(unlock_3, "%s: read unlock 3", tag);
    PRINTF(count_after, "%s: lock count after the three read unlocks", tag);
    sceKernelDeleteRWLock(rwlock);
}

// Main holds a read lock and a writer waits behind it. A read lock fails at once behind the writer.
// Main's read unlock must give the lock to the writer, and nothing may be queued after its unlock.
static void case_c(SceUInt timeout, int cb) {
    char tag[64];
    make_tag(tag, sizeof(tag), 'c', timeout, cb);
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    SceUInt wait = TEST_WAIT_US;
    sceKernelLockReadRWLock(rwlock, &wait);
    writer_may_unlock = 0;
    const SceUID writer = start_thread(write_waiter, LOW_PRIORITY);
    TEST_WAIT_FOR(waits_for_rwlock(writer));
    fail_kind = READ_LOCK;
    fail_cb = cb;
    fail_timeout = timeout;
    test_join_thread(start_thread(failing_entry, LOW_PRIORITY));
    const int failed = fail_result;

    SceKernelRWLockInfo info = { .size = sizeof(info) };
    sceKernelGetRWLockInfo(rwlock, &info);
    const int readers = info.numReadWaitThreads;
    const int writers = info.numWriteWaitThreads;
    const int count_before = info.lockCount;

    const int unlock = sceKernelUnlockReadRWLock(rwlock);
    sceKernelGetRWLockInfo(rwlock, &info);
    const int owner = owner_code(info.writeOwnerId);
    const int writers_after_unlock = info.numWriteWaitThreads;

    writer_may_unlock = 1;
    test_join_thread(writer);
    const int writer_result = waiter_result;
    sceKernelGetRWLockInfo(rwlock, &info);
    const int count_after = info.lockCount;
    const int readers_after = info.numReadWaitThreads;
    const int writers_after = info.numWriteWaitThreads;

    PRINTF(failed, "%s: the read lock that fails at once", tag);
    PRINTF(readers, "%s: waiting readers before the read unlock", tag);
    PRINTF(writers, "%s: waiting writers before the read unlock", tag);
    PRINTF(count_before, "%s: lock count before the read unlock", tag);
    PRINTF(unlock, "%s: the read unlock", tag);
    PRINTF(owner, "%s: write owner after the read unlock (0 none, 1 main, 2 other)", tag);
    PRINTF(writers_after_unlock, "%s: waiting writers after the read unlock", tag);
    PRINTF(writer_result, "%s: the writer's lock", tag);
    PRINTF(count_after, "%s: lock count after the writer's unlock", tag);
    PRINTF(readers_after, "%s: waiting readers after the writer's unlock", tag);
    PRINTF(writers_after, "%s: waiting writers after the writer's unlock", tag);
    sceKernelDeleteRWLock(rwlock);
}

// Main holds a mutex and a lock on it fails at once. Main's unlock leaves the mutex free for a
// third thread's try lock, and no thread waits for it.
static void case_d(SceUInt timeout, int cb) {
    char tag[64];
    make_tag(tag, sizeof(tag), 'd', timeout, cb);
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    SceUInt wait = TEST_WAIT_US;
    sceKernelLockMutex(mutex, 1, &wait);
    fail_kind = MUTEX_LOCK;
    fail_cb = cb;
    fail_timeout = timeout;
    test_join_thread(start_thread(failing_entry, LOW_PRIORITY));
    const int failed = fail_result;

    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    const int count_before = info.currentCount;
    const int owner = owner_code(info.currentOwnerId);
    const int waiting_before = info.numWaitThreads;

    const int unlock = sceKernelUnlockMutex(mutex, 1);
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    const int count_after = info.currentCount;
    const int owner_after = owner_code(info.currentOwnerId);
    const int waiting_after = info.numWaitThreads;

    test_join_thread(start_thread(try_mutex, LOW_PRIORITY));
    const int third = try_result;

    PRINTF(failed, "%s: the mutex lock that fails at once", tag);
    PRINTF(count_before, "%s: lock count before the unlock", tag);
    PRINTF(owner, "%s: owner before the unlock (0 none, 1 main, 2 other)", tag);
    PRINTF(waiting_before, "%s: waiting threads before the unlock", tag);
    PRINTF(unlock, "%s: the unlock", tag);
    PRINTF(count_after, "%s: lock count after the unlock", tag);
    PRINTF(owner_after, "%s: owner after the unlock (0 none, 1 main, 2 other)", tag);
    PRINTF(waiting_after, "%s: waiting threads after the unlock", tag);
    PRINTF(third, "%s: a third thread's try lock", tag);
    sceKernelDeleteMutex(mutex);
}

int main(void) {
    main_thread = sceKernelGetThreadId();
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    for (SceUInt timeout = 0; timeout <= 1; timeout++) {
        for (int cb = 0; cb <= 1; cb++) {
            case_a(timeout, cb);
            case_b(timeout, cb);
            case_c(timeout, cb);
            case_d(timeout, cb);
        }
    }

    test_exit();
    return 0;
}
