// What the relock of a condition variable waiter does when a callback took the mutex during the
// wait. A thread T locks a mutex M and waits on a condition C with callbacks, which releases M.
// The main thread notifies T's callback, waits until it ran and T waits on C again, then signals
// C. The cases use a heavy or a lightweight condition, as they say, and every case has fresh
// objects. Every thread is pinned to core 0, with the main thread at 0x60 and T at 0xB0. The cases:
// - a recursive heavy mutex that the callback locks once. The relock adds back the one level the
//   wait released, so the count is 2 after the wait. T then unlocks three times.
// - a recursive heavy mutex that the callback locks 0x7FFFFFFF times, so the relock's add
//   overflows. T unlocks the whole count.
// - a plain lightweight mutex that the callback locks.
// - a recursive lightweight mutex that the callback locks once. T unlocks it twice.
// - a callback that is pending when T waits. It runs at the start of the wait, after the wait
//   released M, and records the mutex's owner, the condition's waiters, T's wait type and the
//   result of a try lock. The callback either gives the lock back or keeps it. Heavy and
//   lightweight.
// - the main thread holds the mutex and has its own callback pending while it signals, then
//   unlocks. It prints the runs of the callback before and after sceKernelCheckCallback. Heavy
//   and lightweight.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define T_PRIORITY 0xB0
#define NOT_SET 0xDEAD
#define WAIT_US (2 * TEST_WAIT_US)

typedef enum Kind { HEAVY, LIGHT } Kind;

static Kind kind;
static SceUID mutex, cond, callback;
static SceKernelLwMutexWork *lw_mutex;
static SceKernelLwCondWork *lw_cond;
static SceKernelCallbackFunction callback_fn;
// The levels the callback of a take case locks, and whether T's callback is pending at its wait
static int callback_takes, notify_self, keeps_lock;
// Whether T unlocks the whole count at once, or one level at a time, how many times
static int unlock_whole, unlock_times;

static volatile int callback_lock, callback_runs, callback_owner, callback_waiters;
static volatile int callback_wait_type, callback_try;
static volatile int wait_result, runs_in_wait, owns, held_count, done;
static volatile int unlocks[3];

// Locks the mutex of the case
static int mutex_lock(int levels) {
    if (kind == HEAVY)
        return sceKernelLockMutex(mutex, levels, NULL);
    return sceKernelLockLwMutex(lw_mutex, levels, NULL);
}

// Try locks the mutex of the case once
static int mutex_try_lock(void) {
    if (kind == HEAVY)
        return sceKernelTryLockMutex(mutex, 1);
    return sceKernelTryLockLwMutex(lw_mutex, 1);
}

// Unlocks the mutex of the case by levels
static int mutex_unlock(int levels) {
    if (kind == HEAVY)
        return sceKernelUnlockMutex(mutex, levels);
    return sceKernelUnlockLwMutex(lw_mutex, levels);
}

// Waits on the condition of the case with callbacks
static int cond_wait_cb(SceUInt *timeout) {
    if (kind == HEAVY)
        return sceKernelWaitCondCB(cond, timeout);
    return sceKernelWaitLwCondCB(lw_cond, timeout);
}

static int cond_signal(void) {
    if (kind == HEAVY)
        return sceKernelSignalCond(cond);
    return sceKernelSignalLwCond(lw_cond);
}

static int cond_signal_all(void) {
    if (kind == HEAVY)
        return sceKernelSignalCondAll(cond);
    return sceKernelSignalLwCondAll(lw_cond);
}

static int cond_signal_to(SceUID thread) {
    if (kind == HEAVY)
        return sceKernelSignalCondTo(cond, thread);
    return sceKernelSignalLwCondTo(lw_cond, thread);
}

// The wait type of a waiter on the condition of the case, without callbacks
static SceUInt32 cond_wait_type(void) {
    return kind == HEAVY ? WAIT_TYPE_COND : WAIT_TYPE_LW_COND;
}

// The UID a waiter on the condition of the case reports
static SceUID cond_id(void) {
    if (kind == HEAVY)
        return cond;
    LwCondInfo info = { .size = sizeof(info) };
    return sceKernelGetLwCondInfo(lw_cond, &info) == 0 ? info.uid : -1;
}

// The owner of the mutex of the case, 0 when it is free
static SceUID mutex_owner(void) {
    if (kind == HEAVY) {
        MutexInfo info = { .size = sizeof(info) };
        if (sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info) < 0)
            return -1;
        return info.currentOwnerId;
    }
    LwMutexInfo info = { .size = sizeof(info) };
    return sceKernelGetLwMutexInfo(lw_mutex, &info) == 0 ? info.currentOwnerId : -1;
}

// The count of the mutex of the case
static int mutex_count(void) {
    if (kind == HEAVY) {
        MutexInfo info = { .size = sizeof(info) };
        if (sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info) < 0)
            return -1;
        return info.currentCount;
    }
    LwMutexInfo info = { .size = sizeof(info) };
    return sceKernelGetLwMutexInfo(lw_mutex, &info) == 0 ? info.currentCount : -1;
}

// The number of threads waiting on the condition of the case
static int cond_waiters(void) {
    if (kind == HEAVY) {
        SceKernelCondInfo info = { .size = sizeof(info) };
        return sceKernelGetCondInfo(cond, &info) == 0 ? info.numWaitThreads : -1;
    }
    LwCondInfo info = { .size = sizeof(info) };
    return sceKernelGetLwCondInfo(lw_cond, &info) == 0 ? (int)info.numWaitThreads : -1;
}

// The wait type a thread reports, 0 when it doesn't wait
static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? info.waitType : 0;
}

// Runs the callback of a take case: the lock it takes, if any
static int take_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    if (callback_takes)
        callback_lock = mutex_lock(callback_takes);
    callback_runs++;
    return 0;
}

// Records the state of the mutex and the condition while T's wait runs its callbacks, then try
// locks the mutex and gives the lock back unless the case keeps it
static int pending_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_owner = mutex_owner();
    callback_waiters = cond_waiters();
    callback_wait_type = wait_type_of(sceKernelGetThreadId());
    callback_try = mutex_try_lock();
    if (callback_try == 0 && !keeps_lock)
        mutex_unlock(1);
    callback_runs++;
    return 0;
}

// Counts the runs of the callback of the main thread
static int count_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    return 0;
}

// Locks the mutex, notifies its own callback when the case says so, waits on the condition with
// callbacks, then records the wait and the mutex, and unlocks as the case says
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_fn, NULL);
    mutex_lock(1);
    if (notify_self)
        sceKernelNotifyCallback(callback, 1);
    SceUInt timeout = WAIT_US;
    wait_result = cond_wait_cb(&timeout);
    runs_in_wait = callback_runs;
    owns = mutex_owner() == sceKernelGetThreadId();
    held_count = mutex_count();
    if (unlock_whole) {
        unlocks[0] = mutex_unlock(held_count);
    } else {
        for (int i = 0; i < unlock_times; i++)
            unlocks[i] = mutex_unlock(1);
    }
    sceKernelDeleteCallback(callback);
    done = 1;
    return 0;
}

static SceUID start_thread(void) {
    const SceUID thread = sceKernelCreateThread("thread", t_entry, T_PRIORITY, 0x4000, 0,
                                                SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

// Creates the mutex of the case, recursive when asked, and the condition on it
static void create_objects(int recursive, SceKernelLwMutexWork *m, SceKernelLwCondWork *c) {
    lw_mutex = m;
    lw_cond = c;
    if (kind == HEAVY) {
        mutex = sceKernelCreateMutex("mutex", recursive ? SCE_KERNEL_MUTEX_ATTR_RECURSIVE : 0, 0,
                                     NULL);
        cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    } else {
        sceKernelCreateLwMutex(m, "lw mutex", recursive ? LW_MUTEX_ATTR_RECURSIVE : 0, 0, NULL);
        sceKernelCreateLwCond(c, "lw cond", 0, m, NULL);
    }
}

// Deletes the heavy objects of the case. The lightweight ones are left alone.
static void finish(void) {
    if (kind == HEAVY) {
        sceKernelDeleteCond(cond);
        sceKernelDeleteMutex(mutex);
    }
}

// Resets what the threads record for a case
static void reset(void) {
    callback_runs = 0;
    callback_lock = callback_owner = callback_waiters = NOT_SET;
    callback_wait_type = callback_try = NOT_SET;
    wait_result = runs_in_wait = owns = held_count = NOT_SET;
    done = 0;
    for (int i = 0; i < 3; i++)
        unlocks[i] = NOT_SET;
}

// The callback notified while T waits takes the mutex, and T's relock finds it owned by itself
static void take_case(const char *name, Kind k, int recursive, int takes, int whole, int times) {
    kind = k;
    SceKernelLwMutexWork m;
    SceKernelLwCondWork c;
    create_objects(recursive, &m, &c);
    callback_fn = take_callback;
    callback_takes = takes;
    notify_self = 0;
    keeps_lock = 0;
    unlock_whole = whole;
    unlock_times = times;
    reset();

    const SceUID t = start_thread();
    TEST_WAIT_FOR(test_in_wait(t, cond_wait_type() | WAIT_TYPE_CB, cond_id()));
    sceKernelNotifyCallback(callback, 1);
    const SceUInt32 waiting = cond_wait_type() | WAIT_TYPE_CB;
    TEST_WAIT_FOR(callback_runs == 1 && test_in_wait(t, waiting, cond_id()));
    const int waits_again = test_in_wait(t, waiting, cond_id());
    const int signal = cond_signal();
    TEST_WAIT_FOR(done);
    if (done)
        test_join_thread(t);

    PRINTF(waits_again, "%s: T waits on the condition again after the callback", name);
    PRINTF(callback_lock, "%s: the callback's lock", name);
    PRINTF(signal, "%s: the signal", name);
    PRINTF(wait_result, "%s: T's wait", name);
    PRINTF(runs_in_wait, "%s: runs of the callback when the wait returned", name);
    PRINTF(owns, "%s: T owns the mutex after the wait", name);
    PRINTF(held_count, "%s: the count after the wait", name);
    if (whole) {
        PRINTF(unlocks[0], "%s: T's unlock of the whole count", name);
    } else {
        for (int i = 0; i < times; i++)
            PRINTF(unlocks[i], "%s: T's unlock %d", name, i + 1);
    }
    finish();
}

// T's callback is pending when it waits, so it runs before the wait sleeps
static void pending_case(const char *name, Kind k, int keeps) {
    kind = k;
    SceKernelLwMutexWork m;
    SceKernelLwCondWork c;
    create_objects(0, &m, &c);
    callback_fn = pending_callback;
    callback_takes = 0;
    notify_self = 1;
    keeps_lock = keeps;
    unlock_whole = 0;
    unlock_times = 1;
    reset();

    const SceUID t = start_thread();
    TEST_WAIT_FOR(test_in_wait(t, cond_wait_type() | WAIT_TYPE_CB, cond_id()));
    const int signal = cond_signal();
    TEST_WAIT_FOR(done);
    if (done)
        test_join_thread(t);

    PRINTF(callback_owner == 0, "%s: the callback finds the mutex without an owner", name);
    PRINTF(callback_waiters, "%s: the condition's waiters in the callback", name);
    PRINTF(callback_wait_type, "%s: T's wait type in the callback", name);
    PRINTF(callback_try, "%s: the callback's try lock", name);
    PRINTF(signal, "%s: the signal", name);
    PRINTF(wait_result, "%s: T's wait", name);
    PRINTF(owns, "%s: T owns the mutex after the wait", name);
    PRINTF(unlocks[0], "%s: T's unlock", name);
    finish();
}

// The main thread holds the mutex and signals with its own callback pending, then unlocks
static void signaler_case(const char *name, Kind k) {
    kind = k;
    SceKernelLwMutexWork m;
    SceKernelLwCondWork c;
    create_objects(0, &m, &c);
    callback = sceKernelCreateCallback("signaler", 0, count_callback, NULL);
    callback_runs = 0;
    mutex_lock(1);
    sceKernelNotifyCallback(callback, 1);
    const int signal = cond_signal();
    const int signal_all = cond_signal_all();
    const int signal_to = cond_signal_to(sceKernelGetThreadId());
    const int unlock = mutex_unlock(1);
    const int runs_before = callback_runs;
    const int check = sceKernelCheckCallback();
    const int runs_after = callback_runs;
    sceKernelDeleteCallback(callback);

    PRINTF(signal, "%s: the signal", name);
    PRINTF(signal_all, "%s: the signal to all", name);
    PRINTF(signal_to, "%s: the signal to the main thread", name);
    PRINTF(unlock, "%s: the unlock", name);
    PRINTF(runs_before, "%s: runs of the callback before the check", name);
    PRINTF(check, "%s: the check", name);
    PRINTF(runs_after, "%s: runs of the callback after the check", name);
    finish();
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    take_case("heavy recursive mutex, the callback locks it once", HEAVY, 1, 1, 0, 3);
    take_case("heavy recursive mutex, the callback locks 0x7FFFFFFF times", HEAVY, 1, 0x7FFFFFFF,
              1, 0);
    take_case("lightweight recursive mutex, the callback locks it once", LIGHT, 1, 1, 0, 2);
    pending_case("heavy, callback pending at the wait, the callback gives the lock back", HEAVY, 0);
    pending_case("lightweight, callback pending at the wait, gives the lock back", LIGHT, 0);
    signaler_case("heavy, the signaler's callback is pending", HEAVY);
    signaler_case("lightweight, the signaler's callback is pending", LIGHT);
    // These relocks find a plain mutex that T owns. They go last, as a wrong result could hang them.
    take_case("lightweight mutex, the callback locks it", LIGHT, 0, 1, 0, 1);
    pending_case("heavy, callback pending at the wait, the callback keeps the lock", HEAVY, 1);
    pending_case("lightweight, callback pending at the wait, keeps the lock", LIGHT, 1);

    test_exit();
    return 0;
}
