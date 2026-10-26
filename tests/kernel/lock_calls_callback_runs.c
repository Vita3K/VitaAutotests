// Which mutex and rwlock calls run a callback that is pending on the calling thread, and which
// leave it pending. Each case makes the callback pending, makes one call, and prints the call's
// result, the runs of the callback when the call returned, and the runs after a check of the
// callbacks. A call that leaves the callback pending has 0 runs at its return and 1 run after the
// check. The cases:
// - a plain rwlock that the main thread owns for writing: the write lock and the read lock with
//   callbacks, then the write unlock
// - a recursive rwlock that the main thread owns for writing: the write lock with callbacks, which
//   takes a second lock, then the lock count and two write unlocks
// - a free rwlock: a try write lock, a write unlock, a read lock, a try read lock, two read
//   unlocks, a write lock, a write unlock, an info call and a cancel with flag 0
// - a rwlock that another thread holds for writing: a write lock with a timeout of 1000 us
// - a free mutex: a lock, the owner's lock again, a cancel to count 1 with an info call, an
//   unlock, a try lock and an unlock
// - a mutex that another thread holds: a lock with a timeout of 1000 us and a try lock
//
// The callback only counts its runs. Each call that could wait has a finite timeout, so a wrong
// implementation can't hang the test. The two calls that wait on a held object use 1000 us.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_WRITTEN 0x7777

static SceUID callback, release, rwlock, held_rwlock, mutex, held_mutex;
static volatile int calls, held_ready;

static int counting_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    calls++;
    return 0;
}

// Takes the write lock of held_rwlock, and keeps it until release is signaled
static int rwlock_holder(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelLockWriteRWLock(held_rwlock, &timeout);
    held_ready = 1;
    timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(release, 1, &timeout);
    sceKernelUnlockWriteRWLock(held_rwlock);
    return 0;
}

// Takes held_mutex, and keeps it until release is signaled
static int mutex_holder(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelLockMutex(held_mutex, 1, &timeout);
    held_ready = 1;
    timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(release, 1, &timeout);
    sceKernelUnlockMutex(held_mutex, 1);
    return 0;
}

// Makes the callback pending for the call that comes next
static void make_pending(void) {
    calls = 0;
    sceKernelNotifyCallback(callback, 1);
}

// Prints a call's result and the callback's runs at its return and after a check, then cancels it
static void print_call(const char *label, int result) {
    const int ran = calls;
    sceKernelCheckCallback();
    PRINTF(result, "%s: the result", label);
    PRINTF(ran, "%s: runs of the pending callback", label);
    PRINTF(calls, "%s: runs after a check of the callbacks", label);
    sceKernelCancelCallback(callback);
}

static int read_info(SceUID id, MutexInfo *info) {
    info->size = sizeof(*info);
    return sceKernelGetMutexInfo(id, (SceKernelMutexInfo *)info);
}

int main(void) {
    SceUInt timeout;
    int waiters;
    MutexInfo mutex_info;

    callback = sceKernelCreateCallback("pending", 0, counting_callback, NULL);
    release = sceKernelCreateSema("release", 0, 0, 1, NULL);

    // A plain rwlock that the main thread owns for writing, with the calls with callbacks on it
    rwlock = sceKernelCreateRWLock("plain", 0, NULL);
    timeout = TEST_WAIT_US;
    sceKernelLockWriteRWLock(rwlock, &timeout);
    make_pending();
    timeout = TEST_WAIT_US;
    print_call("plain rwlock, write owner: write lock with callbacks",
               sceKernelLockWriteRWLockCB(rwlock, &timeout));
    make_pending();
    timeout = TEST_WAIT_US;
    print_call("plain rwlock, write owner: read lock with callbacks",
               sceKernelLockReadRWLockCB(rwlock, &timeout));
    make_pending();
    print_call("plain rwlock, write owner: write unlock", sceKernelUnlockWriteRWLock(rwlock));
    sceKernelDeleteRWLock(rwlock);

    // A recursive rwlock owned for writing by the main thread, which takes a second write lock
    rwlock = sceKernelCreateRWLock("recursive", RW_LOCK_ATTR_RECURSIVE, NULL);
    timeout = TEST_WAIT_US;
    sceKernelLockWriteRWLock(rwlock, &timeout);
    make_pending();
    timeout = TEST_WAIT_US;
    print_call("recursive rwlock, write owner: write lock with callbacks",
               sceKernelLockWriteRWLockCB(rwlock, &timeout));
    SceKernelRWLockInfo rw_info = { .size = sizeof(rw_info) };
    const int info_result = sceKernelGetRWLockInfo(rwlock, &rw_info);
    PRINT("recursive rwlock, write owner: info after the call", info_result);
    PRINT("recursive rwlock, write owner: lock count after the call", rw_info.lockCount);
    PRINT("recursive rwlock: first write unlock", sceKernelUnlockWriteRWLock(rwlock));
    PRINT("recursive rwlock: second write unlock", sceKernelUnlockWriteRWLock(rwlock));
    sceKernelDeleteRWLock(rwlock);

    // A free rwlock, each call with the callback pending
    rwlock = sceKernelCreateRWLock("free", 0, NULL);
    make_pending();
    print_call("free rwlock: try write lock", sceKernelTryLockWriteRWLock(rwlock));
    make_pending();
    print_call("free rwlock: write unlock", sceKernelUnlockWriteRWLock(rwlock));
    make_pending();
    timeout = TEST_WAIT_US;
    print_call("free rwlock: read lock", sceKernelLockReadRWLock(rwlock, &timeout));
    make_pending();
    print_call("free rwlock: try read lock", sceKernelTryLockReadRWLock(rwlock));
    make_pending();
    print_call("free rwlock: read unlock", sceKernelUnlockReadRWLock(rwlock));
    make_pending();
    print_call("free rwlock: read unlock again", sceKernelUnlockReadRWLock(rwlock));
    make_pending();
    timeout = TEST_WAIT_US;
    print_call("free rwlock: write lock", sceKernelLockWriteRWLock(rwlock, &timeout));
    make_pending();
    print_call("free rwlock: write unlock after the write lock", sceKernelUnlockWriteRWLock(rwlock));
    make_pending();
    print_call("free rwlock: info", sceKernelGetRWLockInfo(rwlock, &rw_info));
    PRINT("free rwlock: lock count after the info call", rw_info.lockCount);
    SceUInt32 readers = NOT_WRITTEN, writers = NOT_WRITTEN;
    make_pending();
    print_call("free rwlock: cancel with flag 0", sceKernelCancelRWLock(rwlock, &readers, &writers, 0));
    PRINT("free rwlock: readers the cancel writes", readers);
    PRINT("free rwlock: writers the cancel writes", writers);
    sceKernelDeleteRWLock(rwlock);

    // A rwlock another thread holds for writing, so the plain write lock waits and times out
    held_rwlock = sceKernelCreateRWLock("held", 0, NULL);
    held_ready = 0;
    const SceUID rwlock_thread = test_start_thread(rwlock_holder);
    TEST_WAIT_FOR(held_ready);
    make_pending();
    timeout = 1000;
    print_call("rwlock held by another thread: write lock with a timeout of 1000 us",
               sceKernelLockWriteRWLock(held_rwlock, &timeout));
    sceKernelSignalSema(release, 1);
    test_join_thread(rwlock_thread);
    sceKernelDeleteRWLock(held_rwlock);

    // A free mutex, the owner's calls, and a cancel to count 1 that keeps the owner
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    make_pending();
    timeout = TEST_WAIT_US;
    print_call("free mutex: lock", sceKernelLockMutex(mutex, 1, &timeout));
    make_pending();
    timeout = TEST_WAIT_US;
    print_call("owned mutex: lock again", sceKernelLockMutex(mutex, 1, &timeout));
    make_pending();
    waiters = NOT_WRITTEN;
    print_call("owned mutex: cancel to count 1", sceKernelCancelMutex(mutex, 1, &waiters));
    PRINT("owned mutex: waiters the cancel writes", waiters);
    make_pending();
    print_call("owned mutex: info", read_info(mutex, &mutex_info));
    PRINT("owned mutex: count after the info call", mutex_info.currentCount);
    make_pending();
    print_call("owned mutex: unlock", sceKernelUnlockMutex(mutex, 1));
    make_pending();
    print_call("free mutex: try lock", sceKernelTryLockMutex(mutex, 1));
    make_pending();
    print_call("owned mutex: unlock after the try lock", sceKernelUnlockMutex(mutex, 1));
    sceKernelDeleteMutex(mutex);

    // A mutex another thread holds, so the plain lock waits and times out, and a try lock fails
    held_mutex = sceKernelCreateMutex("held", 0, 0, NULL);
    held_ready = 0;
    const SceUID mutex_thread = test_start_thread(mutex_holder);
    TEST_WAIT_FOR(held_ready);
    make_pending();
    timeout = 1000;
    print_call("mutex held by another thread: lock with a timeout of 1000 us",
               sceKernelLockMutex(held_mutex, 1, &timeout));
    make_pending();
    print_call("mutex held by another thread: try lock", sceKernelTryLockMutex(held_mutex, 1));
    sceKernelSignalSema(release, 1);
    test_join_thread(mutex_thread);
    sceKernelDeleteMutex(held_mutex);

    sceKernelDeleteSema(release);
    sceKernelDeleteCallback(callback);
    test_exit();
    return 0;
}
