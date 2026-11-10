// The waits callback_outer_wake leaves out: a read lock and a write lock, a lightweight mutex, a
// condition variable, a lightweight condition variable and a thread end. A thread waits with
// callbacks, the callback it runs blocks in a wait of its own, and the test frees what the first
// wait waits for in between. It reads what the object reports, tries to take it itself, then ends
// the callback's wait and prints how the first wait ended. A signal that misses the first wait
// leaves it waiting until its timeout, which the test then sees as the result. Those waits get a
// shorter timeout, and the test ends the callback's wait before it prints anything there.
//
// On a PS Vita TV on firmware 3.65 no object counts the thread as a waiter while its callback
// runs, and nothing is handed to it. An unlock leaves the rwlock or the lightweight mutex free,
// the test thread can lock it, a writer in its callback doesn't hold back a new reader, and the
// first wait takes the lock once the callback returns. A condition variable signal finds no
// waiter, a signal to the thread fails with 0x80028022, and the first wait goes on until its
// timeout. The callback runs without the condition's mutex. A thread that ends during the
// callback ends the first wait with its exit status once the callback returns.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

enum Kind { READ_AFTER_WRITE, WRITE_AFTER_WRITE, WRITE_AFTER_READ, LW_MUTEX, COND, COND_TO, LW_COND, LW_COND_TO, THREAD_END };

static SceUID rwlock, cond, cond_mutex, ended, ended_release, inner, cb;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static volatile int inner_result, outer_result, unlock_result;
static SceUInt outer_left;
static int end_status;

// Blocks in a wait on another semaphore, until the test signals it
static int inner_wait_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    SceUInt timeout = 2 * TEST_WAIT_US;
    inner_result = sceKernelWaitSema(inner, 1, &timeout);
    return 0;
}

static int outer_thread(SceSize args, void *argp) {
    (void)args;
    cb = sceKernelCreateCallback("inner", 0, inner_wait_callback, NULL);
    outer_left = 4 * TEST_WAIT_US;
    switch (*(const int *)argp) {
    case READ_AFTER_WRITE:
        outer_result = sceKernelLockReadRWLockCB(rwlock, &outer_left);
        if (outer_result == 0)
            sceKernelUnlockReadRWLock(rwlock);
        break;
    case WRITE_AFTER_WRITE:
    case WRITE_AFTER_READ:
        outer_result = sceKernelLockWriteRWLockCB(rwlock, &outer_left);
        if (outer_result == 0)
            sceKernelUnlockWriteRWLock(rwlock);
        break;
    case LW_MUTEX:
        outer_result = sceKernelLockLwMutexCB(&lw_mutex, 1, &outer_left);
        if (outer_result == 0)
            sceKernelUnlockLwMutex(&lw_mutex, 1);
        break;
    case COND:
    case COND_TO:
        // The signal misses this wait, so it only has to outlast the callback
        outer_left = TEST_WAIT_US;
        sceKernelLockMutex(cond_mutex, 1, NULL);
        outer_result = sceKernelWaitCondCB(cond, &outer_left);
        unlock_result = sceKernelUnlockMutex(cond_mutex, 1);
        break;
    case LW_COND:
    case LW_COND_TO:
        outer_left = TEST_WAIT_US;
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        outer_result = sceKernelWaitLwCondCB(&lw_cond, &outer_left);
        unlock_result = sceKernelUnlockLwMutex(&lw_mutex, 1);
        break;
    case THREAD_END: outer_result = sceKernelWaitThreadEndCB(ended, &end_status, &outer_left); break;
    }
    sceKernelDeleteCallback(cb);
    return 0;
}

// Ends with status 5 once the test signals it
static int ending_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelWaitSema(ended_release, 1, NULL);
    return 5;
}

static int sema_count(SceUID id) {
    SceKernelSemaInfo info = { .size = sizeof(info) };
    sceKernelGetSemaInfo(id, &info);
    return info.currentCount;
}

static SceKernelRWLockInfo rwlock_info(void) {
    SceKernelRWLockInfo info = { .size = sizeof(info) };
    sceKernelGetRWLockInfo(rwlock, &info);
    return info;
}

static LwMutexInfo lw_mutex_info(void) {
    LwMutexInfo info = { .size = sizeof(info) };
    sceKernelGetLwMutexInfo(&lw_mutex, &info);
    return info;
}

// Starts a thread waiting in the given way and makes its callback block on the inner semaphore
static SceUID start_in_callback(int kind) {
    inner = sceKernelCreateSema("inner", 0, 0, 1, NULL);
    inner_result = outer_result = unlock_result = 0xDEAD;
    const SceUID thread = test_start_thread_arg(outer_thread, kind);
    test_wait_until_waiting(thread);
    sceKernelNotifyCallback(cb, 1);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_SEMA, inner));
    return thread;
}

// Ends the callback's wait and waits for the thread to end
static void end_callback(SceUID thread) {
    sceKernelSignalSema(inner, 1);
    test_join_thread(thread);
}

// Prints how both waits ended. If the wake for the outer wait had ended the callback's wait too,
// the signal in end_callback is left over.
static void print_waits(const char *name) {
    PRINTF(inner_result, "%s: the callback's wait", name);
    PRINTF(sema_count(inner), "%s: the inner semaphore's count at the end", name);
    PRINTF(outer_result, "%s: the outer wait", name);
    PRINTF(outer_left > 0, "%s: the outer wait has time left", name);
    sceKernelDeleteSema(inner);
}

static void finish(const char *name, SceUID thread) {
    end_callback(thread);
    print_waits(name);
}

// Prints the rwlock's state while the waiting thread runs its callback
static void print_rwlock(const char *name, const char *when, SceUID thread) {
    const SceKernelRWLockInfo info = rwlock_info();
    PRINTF(info.lockCount, "%s: its lock count %s", name, when);
    PRINTF(info.writeOwnerId == thread, "%s: the waiting thread holds the write lock %s", name, when);
    PRINTF(info.numReadWaitThreads, "%s: its read waiters %s", name, when);
    PRINTF(info.numWriteWaitThreads, "%s: its write waiters %s", name, when);
}

// Tries a read lock and a write lock from the test thread, and gives back whichever it got
static void try_rwlock(const char *name, const char *when) {
    int result = sceKernelTryLockReadRWLock(rwlock);
    PRINTF(result, "%s: try a read lock %s", name, when);
    if (result == 0)
        sceKernelUnlockReadRWLock(rwlock);
    result = sceKernelTryLockWriteRWLock(rwlock);
    PRINTF(result, "%s: try a write lock %s", name, when);
    if (result == 0)
        sceKernelUnlockWriteRWLock(rwlock);
}

static void rwlock_case(const char *name, int kind) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    const int read_held = kind == WRITE_AFTER_READ;
    if (read_held)
        sceKernelLockReadRWLock(rwlock, NULL);
    else
        sceKernelLockWriteRWLock(rwlock, NULL);
    const SceUID thread = start_in_callback(kind);
    print_rwlock(name, "while the callback waits", thread);
    // A read lock is the one a waiting writer could hold back
    if (read_held) {
        const int result = sceKernelTryLockReadRWLock(rwlock);
        PRINTF(result, "%s: try a second read lock while the callback waits", name);
        if (result == 0)
            sceKernelUnlockReadRWLock(rwlock);
    }
    PRINTF(read_held ? sceKernelUnlockReadRWLock(rwlock) : sceKernelUnlockWriteRWLock(rwlock), "%s: unlock it", name);
    print_rwlock(name, "after the unlock", thread);
    try_rwlock(name, "after the unlock");
    finish(name, thread);
    print_rwlock(name, "at the end", thread);
    sceKernelDeleteRWLock(rwlock);
}

// A condition variable signal, or a signal to the waiting thread. The outer wait runs out, so
// the test keeps the values and prints them once the thread ended.
static void cond_case(const char *name, int kind) {
    cond_mutex = sceKernelCreateMutex("cond mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, cond_mutex, NULL);
    const SceUID thread = start_in_callback(kind);
    SceKernelCondInfo info = { .size = sizeof(info) };
    sceKernelGetCondInfo(cond, &info);
    const int waiters = info.numWaitThreads;
    MutexInfo mutex_info = { .size = sizeof(mutex_info) };
    sceKernelGetMutexInfo(cond_mutex, (SceKernelMutexInfo *)&mutex_info);
    const int try_result = sceKernelTryLockMutex(cond_mutex, 1);
    if (try_result == 0)
        sceKernelUnlockMutex(cond_mutex, 1);
    const int signal_result = kind == COND_TO ? sceKernelSignalCondTo(cond, thread) : sceKernelSignalCond(cond);
    sceKernelGetCondInfo(cond, &info);
    end_callback(thread);
    PRINTF(waiters, "%s: its waiters while the callback waits", name);
    PRINTF(mutex_info.currentOwnerId == thread, "%s: the waiting thread owns the mutex while the callback waits", name);
    PRINTF(mutex_info.currentCount, "%s: the mutex's count while the callback waits", name);
    PRINTF(try_result, "%s: try to lock the mutex while the callback waits", name);
    PRINTF(signal_result, "%s: signal it", name);
    PRINTF(info.numWaitThreads, "%s: its waiters after the signal", name);
    print_waits(name);
    PRINTF(unlock_result, "%s: the waiting thread unlocks the mutex after the wait", name);
    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(cond_mutex);
}

// A lightweight condition variable signal, or a signal to the waiting thread, printed like cond_case
static void lw_cond_case(const char *name, int kind) {
    sceKernelCreateLwMutex(&lw_mutex, "lw cond mutex", 0, 0, NULL);
    sceKernelCreateLwCond(&lw_cond, "lw cond", 0, &lw_mutex, NULL);
    const SceUID thread = start_in_callback(kind);
    LwCondInfo info = { .size = sizeof(info) };
    sceKernelGetLwCondInfo(&lw_cond, &info);
    const int waiters = info.numWaitThreads;
    const LwMutexInfo mutex_info = lw_mutex_info();
    const int try_result = sceKernelTryLockLwMutex(&lw_mutex, 1);
    if (try_result == 0)
        sceKernelUnlockLwMutex(&lw_mutex, 1);
    const int signal_result = kind == LW_COND_TO ? sceKernelSignalLwCondTo(&lw_cond, thread) : sceKernelSignalLwCond(&lw_cond);
    sceKernelGetLwCondInfo(&lw_cond, &info);
    end_callback(thread);
    PRINTF(waiters, "%s: its waiters while the callback waits", name);
    PRINTF(mutex_info.currentOwnerId == thread, "%s: the waiting thread owns the mutex while the callback waits", name);
    PRINTF(mutex_info.currentCount, "%s: the mutex's count while the callback waits", name);
    PRINTF(try_result, "%s: try to lock the mutex while the callback waits", name);
    PRINTF(signal_result, "%s: signal it", name);
    PRINTF(info.numWaitThreads, "%s: its waiters after the signal", name);
    print_waits(name);
    PRINTF(unlock_result, "%s: the waiting thread unlocks the mutex after the wait", name);
    sceKernelDeleteLwCond(&lw_cond);
    sceKernelDeleteLwMutex(&lw_mutex);
}

int main(void) {
    // A read lock waiting for a writer, and a write lock waiting for a writer or a reader
    rwlock_case("read lock after a write lock", READ_AFTER_WRITE);
    rwlock_case("write lock after a write lock", WRITE_AFTER_WRITE);
    rwlock_case("write lock after a read lock", WRITE_AFTER_READ);

    // A lightweight mutex unlock
    sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 0, NULL);
    sceKernelLockLwMutex(&lw_mutex, 1, NULL);
    SceUID thread = start_in_callback(LW_MUTEX);
    LwMutexInfo mutex_info = lw_mutex_info();
    PRINT("lightweight mutex: its waiters while the callback waits", mutex_info.numWaitThreads);
    PRINT("lightweight mutex: unlock it", sceKernelUnlockLwMutex(&lw_mutex, 1));
    mutex_info = lw_mutex_info();
    PRINT("lightweight mutex: the waiting thread owns it after the unlock", mutex_info.currentOwnerId == thread);
    PRINT("lightweight mutex: its count after the unlock", mutex_info.currentCount);
    PRINT("lightweight mutex: its waiters after the unlock", mutex_info.numWaitThreads);
    const int try_result = sceKernelTryLockLwMutex(&lw_mutex, 1);
    PRINT("lightweight mutex: try to lock it", try_result);
    if (try_result == 0)
        sceKernelUnlockLwMutex(&lw_mutex, 1);
    finish("lightweight mutex", thread);
    sceKernelDeleteLwMutex(&lw_mutex);

    cond_case("condition variable", COND);
    cond_case("condition variable signal to the thread", COND_TO);
    lw_cond_case("lightweight condition variable", LW_COND);
    lw_cond_case("lightweight condition variable signal to the thread", LW_COND_TO);

    // The end of the thread the outer wait waits for
    ended_release = sceKernelCreateSema("release", 0, 0, 1, NULL);
    ended = test_start_thread(ending_thread);
    end_status = 0xDEAD;
    thread = start_in_callback(THREAD_END);
    sceKernelSignalSema(ended_release, 1);
    sceKernelWaitThreadEnd(ended, NULL, NULL);
    finish("thread end", thread);
    PRINT("thread end: the exit status the outer wait got", end_status);
    sceKernelDeleteThread(ended);
    sceKernelDeleteSema(ended_release);

    test_exit();
    return 0;
}
