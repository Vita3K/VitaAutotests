// What a thread, its mutex and its condition report right after sceKernelNotifyCallback returns,
// before the main thread makes a call that blocks or prints. Thread T locks a mutex M and waits on
// a condition C with callbacks and a timeout of 8 s, which releases M. The main thread locks M and
// signals C, so T waits to lock M again, and once T reports that wait the main thread notifies T's
// callback. The callback blocks on a gate with a timeout of 3 s. The notify is the call that
// should take T out of the mutex's queue, so the reads right after it show that state before the
// callback can change anything. They are T's status and wait type, the number of waiters of M, the
// number of waiters of C, and the result of SignalCondTo(T), each taken into a variable with no
// call that blocks or prints in between. Once the callback blocks on the gate, the main thread
// unlocks M, opens the gate and joins T with a bounded wait, and prints the reads, followed by T's
// wait result, the runs of the callback and whether T owns M after its wait. The test does not
// assume what any read returns. A woken thread may run while the main thread reads, so the reads
// of T's status and wait type and the waiter count of M can change from one run to the next, and
// they are printed all the same. Each of the heavy and the lightweight pair runs on fresh objects
// and a fresh thread.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define WAIT_US 8000000
#define GATE_US 3000000

// Whether the running case uses the lightweight pair. The heavy pair is used otherwise.
static int lightweight;
static SceUID mutex, cond, mutex_id, callback, gate;
// A lightweight mutex is never deleted, so the lightweight case creates it once
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static volatile int callback_ready, callback_runs, t_done;
static volatile int t_result, t_runs, t_owns;

// Counts its run, then blocks on the gate until the main thread opens it
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    SceUInt timeout = GATE_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

static int lock_mutex(void) {
    return lightweight ? sceKernelLockLwMutex(&lw_mutex, 1, NULL) : sceKernelLockMutex(mutex, 1, NULL);
}

static int unlock_mutex(void) {
    return lightweight ? sceKernelUnlockLwMutex(&lw_mutex, 1) : sceKernelUnlockMutex(mutex, 1);
}

static int signal_cond(void) {
    return lightweight ? sceKernelSignalLwCond(&lw_cond) : sceKernelSignalCond(cond);
}

static int signal_cond_to(SceUID thread) {
    return lightweight ? sceKernelSignalLwCondTo(&lw_cond, thread) : sceKernelSignalCondTo(cond, thread);
}

static int wait_cond_cb(SceUInt *timeout) {
    return lightweight ? sceKernelWaitLwCondCB(&lw_cond, timeout) : sceKernelWaitCondCB(cond, timeout);
}

// Returns the owner of the mutex
static SceUID mutex_owner(void) {
    if (lightweight) {
        LwMutexInfo info = { .size = sizeof(info) };
        sceKernelGetLwMutexInfo(&lw_mutex, &info);
        return info.currentOwnerId;
    }
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info.currentOwnerId;
}

// Returns the number of threads waiting for the mutex, a relocking waiter included
static int mutex_waiters(void) {
    if (lightweight) {
        LwMutexInfo info = { .size = sizeof(info) };
        sceKernelGetLwMutexInfo(&lw_mutex, &info);
        return info.numWaitThreads;
    }
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info.numWaitThreads;
}

// Returns the number of threads waiting on the condition
static int cond_waiters(void) {
    if (lightweight) {
        LwCondInfo info = { .size = sizeof(info) };
        sceKernelGetLwCondInfo(&lw_cond, &info);
        return info.numWaitThreads;
    }
    SceKernelCondInfo info = { .size = sizeof(info) };
    sceKernelGetCondInfo(cond, &info);
    return info.numWaitThreads;
}

// Returns whether thread is in a wait of the given type, on id unless id is 0
static int waits_in(SceUID thread, SceUInt32 type, SceUID id) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType == type && (id == 0 || info.waitId == id);
}

// Returns whether thread waits with callbacks for the condition. A lightweight condition's UID
// isn't compared, so only its wait type counts.
static int in_cond(SceUID thread) {
    const SceUInt32 type = (lightweight ? WAIT_TYPE_LW_COND : WAIT_TYPE_COND) | WAIT_TYPE_CB;
    return waits_in(thread, type, lightweight ? 0 : cond);
}

// Returns whether thread waits with callbacks to lock the mutex again after its signal
static int in_relock(SceUID thread) {
    const SceUInt32 type = (lightweight ? WAIT_TYPE_LW_COND_MUTEX : WAIT_TYPE_COND_MUTEX) | WAIT_TYPE_CB;
    return waits_in(thread, type, mutex_id);
}

static const char *kind_name(void) {
    return lightweight ? "lightweight" : "heavy";
}

static SceUID start_thread(SceKernelThreadEntry entry, int priority) {
    const SceUID thread = sceKernelCreateThread("t", entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

// Waits up to 5 s for a thread to end, and deletes it if it did
static void join_thread(SceUID thread) {
    SceUInt timeout = 5 * TEST_WAIT_US;
    if (sceKernelWaitThreadEnd(thread, NULL, &timeout) == 0)
        sceKernelDeleteThread(thread);
}

// Creates the mutex and the condition of the case, or the lightweight pair
static void create_objects(void) {
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    if (lightweight) {
        sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "lw cond", 0, &lw_mutex, NULL);
        LwMutexInfo info = { .size = sizeof(info) };
        sceKernelGetLwMutexInfo(&lw_mutex, &info);
        mutex_id = info.uid;
    } else {
        mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
        cond = sceKernelCreateCond("cond", 0, mutex, NULL);
        mutex_id = mutex;
    }
}

// Deletes the condition, the mutex of the heavy pair and the gate. The lightweight mutex stays.
static void destroy_objects(void) {
    sceKernelDeleteSema(gate);
    if (lightweight) {
        sceKernelDeleteLwCond(&lw_cond);
    } else {
        sceKernelDeleteCond(cond);
        sceKernelDeleteMutex(mutex);
    }
}

// Locks M, waits on C with callbacks, then records how the wait ended, the runs of the callback
// and whether T owns M, and unlocks M
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
    unlock_mutex();
    sceKernelDeleteCallback(callback);
    t_done = 1;
    return 0;
}

static void run_case(void) {
    const char *const kind = kind_name();
    callback_ready = callback_runs = t_done = 0;
    t_result = t_runs = t_owns = 0xDEAD;
    callback = 0;
    create_objects();

    const SceUID t = start_thread(t_entry, WAITER_PRIORITY);
    TEST_WAIT_FOR(callback_ready && in_cond(t));
    lock_mutex();
    const int signal = signal_cond();
    TEST_WAIT_FOR(in_relock(t));
    const int notify = sceKernelNotifyCallback(callback, 1);

    // The reads come right after the notify, with no call that blocks or prints in between
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(t, &info);
    const int status = info.status;
    const int wait_type = info.waitType;
    const int mutex_waiters_at_once = mutex_waiters();
    const int cond_waiters_at_once = cond_waiters();
    const int signal_to = signal_cond_to(t);

    // The callback blocks on the gate before the unlock, so T has left the mutex's queue
    TEST_WAIT_FOR(waits_in(t, WAIT_TYPE_SEMA, gate));
    const int unlock = unlock_mutex();
    sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(t_done);
    join_thread(t);
    destroy_objects();

    PRINTF(signal, "%s: the signal", kind);
    PRINTF(notify, "%s: the notify", kind);
    PRINTF(status, "%s: T's status right after the notify", kind);
    PRINTF(wait_type, "%s: T's wait type right after the notify", kind);
    PRINTF(mutex_waiters_at_once, "%s: the mutex's waiters right after the notify", kind);
    PRINTF(cond_waiters_at_once, "%s: the condition's waiters right after the notify", kind);
    PRINTF(signal_to, "%s: SignalCondTo(T) right after the notify", kind);
    PRINTF(unlock, "%s: the main thread's unlock", kind);
    PRINTF(t_result, "%s: T's wait", kind);
    PRINTF(t_runs, "%s: runs of the callback when the wait returned", kind);
    PRINTF(t_owns, "%s: T owns the mutex after the wait", kind);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    lightweight = 0;
    run_case();
    lightweight = 1;
    run_case();

    test_exit();
    return 0;
}
