// Whether a condition variable waiter that runs above the main thread runs its callback before the
// main thread's next line, when the callback is notified while the waiter waits to lock its mutex
// again. Thread T runs above the main thread, locks a mutex M and waits on a condition C with
// callbacks and a timeout of 8 s, which releases M. The main thread locks M and signals C, so T
// waits to lock M again. Once T waits for M, the main thread records step 0, notifies T's callback,
// and records step 2. The callback records step 1. The main thread then waits until the callback
// ran and T waits for M again, reads T's wait type and the waiters of M, and unlocks M, which T
// takes. The test runs for a heavy mutex and condition and for a lightweight pair, on fresh objects
// and threads. The threads are pinned to core 0. Each pair prints the results of the calls, the
// steps in the order they were recorded, the states the main thread reads, and T's result, the runs
// of the callback when its wait returned, whether T owns M after the wait, and the results of T's
// unlock and of sceKernelCheckCallback. This case was split out of cond_relock_callback_notify.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define ABOVE_PRIORITY 0x50
#define WAIT_US 8000000
#define NOT_DONE 0xDEAD
#define STEP_COUNT 3
#define LW_SLOTS 1

// Whether the running case uses the lightweight pair. The heavy pair is used otherwise.
static int lightweight;
static SceUID mutex, cond, mutex_id, callback;
// Each lightweight case gets its own mutex work area, since a lightweight mutex is never deleted
static SceKernelLwMutexWork lw_mutexes[LW_SLOTS];
static SceKernelLwMutexWork *lw_mutex;
static SceKernelLwCondWork lw_cond;
static int lw_slot;

static volatile int callback_ready, callback_runs, t_done;
static volatile int t_result, t_runs, t_owns, t_unlock, t_check;
static int step_count, steps[STEP_COUNT];

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < STEP_COUNT)
        steps[i] = step;
}

// Counts its run and records step 1
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    record(1);
    return 0;
}

static int lock_mutex(void) {
    return lightweight ? sceKernelLockLwMutex(lw_mutex, 1, NULL) : sceKernelLockMutex(mutex, 1, NULL);
}

static int unlock_mutex(void) {
    return lightweight ? sceKernelUnlockLwMutex(lw_mutex, 1) : sceKernelUnlockMutex(mutex, 1);
}

static int signal_cond(void) {
    return lightweight ? sceKernelSignalLwCond(&lw_cond) : sceKernelSignalCond(cond);
}

static int wait_cond_cb(SceUInt *timeout) {
    return lightweight ? sceKernelWaitLwCondCB(&lw_cond, timeout) : sceKernelWaitCondCB(cond, timeout);
}

// Returns the owner of the mutex
static SceUID mutex_owner(void) {
    if (lightweight) {
        LwMutexInfo info = { .size = sizeof(info) };
        sceKernelGetLwMutexInfo(lw_mutex, &info);
        return info.currentOwnerId;
    }
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info.currentOwnerId;
}

// Returns the number of threads waiting for the mutex, which a relocking waiter counts
static int mutex_waiters(void) {
    if (lightweight) {
        LwMutexInfo info = { .size = sizeof(info) };
        sceKernelGetLwMutexInfo(lw_mutex, &info);
        return info.numWaitThreads;
    }
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info.numWaitThreads;
}

// Returns whether thread is in a wait of the given type, on id unless id is 0
static int waits_in(SceUID thread, SceUInt32 type, SceUID id) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType == type && (id == 0 || info.waitId == id);
}

static SceUInt32 cond_wait_type(void) {
    return (lightweight ? WAIT_TYPE_LW_COND : WAIT_TYPE_COND) | WAIT_TYPE_CB;
}

static SceUInt32 relock_wait_type(void) {
    return (lightweight ? WAIT_TYPE_LW_COND_MUTEX : WAIT_TYPE_COND_MUTEX) | WAIT_TYPE_CB;
}

// Returns whether thread waits with callbacks for the condition. A lightweight condition's UID
// isn't compared, so only its wait type counts.
static int in_cond(SceUID thread) {
    return waits_in(thread, cond_wait_type(), lightweight ? 0 : cond);
}

// Returns whether thread waits with callbacks to lock the mutex again after its signal
static int in_relock(SceUID thread) {
    return waits_in(thread, relock_wait_type(), mutex_id);
}

// Returns the thread info, which holds the wait type the case prints
static SceKernelThreadInfo info_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info;
}

static const char *kind_name(void) {
    return lightweight ? "lightweight" : "heavy";
}

// Waits up to 5 s for a thread to end, and deletes it if it did
static void join_thread(SceUID thread) {
    SceUInt timeout = 5 * TEST_WAIT_US;
    if (sceKernelWaitThreadEnd(thread, NULL, &timeout) == 0)
        sceKernelDeleteThread(thread);
}

// Creates the mutex and the condition of the case, or a lightweight pair with a fresh work area
static void create_objects(void) {
    if (lightweight) {
        lw_mutex = &lw_mutexes[lw_slot++];
        sceKernelCreateLwMutex(lw_mutex, "lw mutex", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "lw cond", 0, lw_mutex, NULL);
        LwMutexInfo info = { .size = sizeof(info) };
        sceKernelGetLwMutexInfo(lw_mutex, &info);
        mutex_id = info.uid;
    } else {
        mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
        cond = sceKernelCreateCond("cond", 0, mutex, NULL);
        mutex_id = mutex;
    }
}

// Deletes the condition. A lightweight mutex is never deleted.
static void destroy_objects(void) {
    if (lightweight) {
        sceKernelDeleteLwCond(&lw_cond);
    } else {
        sceKernelDeleteCond(cond);
        sceKernelDeleteMutex(mutex);
    }
}

static void reset_case(void) {
    callback = 0;
    callback_ready = callback_runs = t_done = 0;
    t_result = t_runs = t_owns = t_unlock = t_check = NOT_DONE;
    step_count = 0;
    for (int i = 0; i < STEP_COUNT; i++)
        steps[i] = NOT_DONE;
}

// Locks M, waits on C with callbacks, then records how the wait ended and what the thread holds
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    lock_mutex();
    callback_ready = 1;
    SceUInt timeout = WAIT_US;
    t_result = wait_cond_cb(&timeout);
    t_runs = callback_runs;
    t_owns = mutex_owner() == sceKernelGetThreadId();
    t_unlock = unlock_mutex();
    t_check = sceKernelCheckCallback();
    sceKernelDeleteCallback(callback);
    t_done = 1;
    return 0;
}

// T runs above the main thread. The main thread records steps around the notify, then waits until
// the callback ran and T waits for M again, reads T's state, and unlocks M, which T takes.
static void case_above(void) {
    const char *const kind = kind_name();
    reset_case();
    create_objects();
    const SceUID t = sceKernelCreateThread("t", t_entry, ABOVE_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(callback_ready && in_cond(t));
    lock_mutex();
    const int signal = signal_cond();
    TEST_WAIT_FOR(in_relock(t));
    record(0);
    const int notify = sceKernelNotifyCallback(callback, 1);
    record(2);
    // Another thread can run T's callback after this, so wait until it ran and T waits again
    TEST_WAIT_FOR(callback_runs != 0 && in_relock(t) && mutex_waiters() == 1);
    const SceKernelThreadInfo after = info_of(t);
    const int relock_after = in_relock(t);
    const int waiters_after = mutex_waiters();
    const int unlock = unlock_mutex();
    TEST_WAIT_FOR(t_done);
    join_thread(t);
    destroy_objects();

    PRINTF(signal, "%s, d: the signal", kind);
    for (int i = 0; i < STEP_COUNT; i++)
        PRINTF(steps[i], "%s, d: step %d", kind, i);
    PRINTF(notify, "%s, d: the notify", kind);
    PRINTF(after.waitType, "%s, d: T's wait type after the notify, once it waits again", kind);
    PRINTF(relock_after, "%s, d: T waits for the mutex after the notify", kind);
    PRINTF(waiters_after, "%s, d: the mutex's waiters after the notify", kind);
    PRINTF(callback_runs, "%s, d: runs of the callback after the notify", kind);
    PRINTF(unlock, "%s, d: the main thread's unlock", kind);
    PRINTF(t_result, "%s, d: T's wait", kind);
    PRINTF(t_runs, "%s, d: runs of the callback when the wait returned", kind);
    PRINTF(t_owns, "%s, d: T owns the mutex after the wait", kind);
    PRINTF(t_unlock, "%s, d: T's unlock", kind);
    PRINTF(t_check, "%s, d: T's check", kind);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    for (lightweight = 0; lightweight < 2; lightweight++)
        case_above();

    test_exit();
    return 0;
}
