// What a condition variable waiter W does when sceKernelSuspendThreadForVM holds it at each step
// of its wait, for a heavy condition (sceKernelCreateCond) and a lightweight one
// (sceKernelCreateLwCond). W has the VM attribute and runs on core 0. The main thread, on core 1,
// suspends W at one step, moves the wait on with a signal, a timeout or a delete, and reads W's
// status and wait type, and the mutex's owner, waiters and a try lock, while W stays suspended.
// Then it resumes W and waits for it to end. Each case runs on fresh objects. The cases:
// - a: W is suspended in its wait. The main thread locks the mutex and signals, then reads W and
//   the mutex while it holds the mutex. It unlocks, reads the owner, tries to lock the mutex and
//   reads W's status again.
// - b: The main thread signals and waits until W reports the wait for the mutex, then suspends
//   W and reads the same things as a, before it unlocks.
// - c: W's wait times out while W is suspended, once with the mutex free and once with the main
//   thread holding it. The main thread polls until W's wait is over, then reads W's status, the
//   owner and a try lock, and unlocks what it holds.
// - d: W is suspended in its wait, and the main thread deletes the condition.
// - e, heavy only and last: W is suspended in its wait, and the main thread deletes the mutex.
//   After W is resumed, the main thread signals the condition, which ends W's wait if the delete
//   hasn't already.
// Each case prints the results of its calls and the state it reads. Every wait in the test has a
// timeout, so a case that doesn't end can't stop the rest of the test.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define WAITER_PRIORITY 0xA0
// Long enough for a suspend and the reads after it to finish before the timeout of a case
#define COND_TIMEOUT_US (2 * TEST_WAIT_US)
#define NOT_SET 0xDEAD
// A lightweight mutex is never deleted, so each lightweight run gets its own work area
#define MAX_LW_RUNS 8

// One kind of condition variable, with the calls that differ
typedef struct Kind {
    const char *name;
    SceUInt32 cond_wait_type;
    SceUInt32 mutex_wait_type;
    void (*create)(void);
    void (*destroy)(void);
    int (*lock)(void);
    int (*unlock)(void);
    int (*try_lock)(void);
    int (*wait)(SceUInt *timeout);
    int (*signal)(void);
    int (*delete_cond)(void);
    SceUID (*owner)(void);
    SceUInt32 (*mutex_waiters)(void);
} Kind;

static SceUID mutex, cond;
static SceKernelLwMutexWork lw_mutexes[MAX_LW_RUNS];
static SceKernelLwCondWork lw_conds[MAX_LW_RUNS];
static SceKernelLwMutexWork *lw_mutex;
static SceKernelLwCondWork *lw_cond;
static int lw_runs;
// The UIDs a wait reports, which the create calls store
static SceUID cond_id, mutex_id;
// Set by a case that deletes an object, so the cleanup doesn't delete it again
static int cond_gone, mutex_gone;
static const Kind *kind;
static volatile int w_result, w_left, w_unlock;

static void heavy_create(void) {
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    mutex_id = mutex;
    cond_id = cond;
    cond_gone = mutex_gone = 0;
}

static void heavy_destroy(void) {
    if (!cond_gone)
        sceKernelDeleteCond(cond);
    if (!mutex_gone)
        sceKernelDeleteMutex(mutex);
}

static int heavy_lock(void) {
    SceUInt timeout = TEST_WAIT_US;
    return sceKernelLockMutex(mutex, 1, &timeout);
}

static int heavy_unlock(void) {
    return sceKernelUnlockMutex(mutex, 1);
}

static int heavy_try_lock(void) {
    return sceKernelTryLockMutex(mutex, 1);
}

static int heavy_wait(SceUInt *timeout) {
    return sceKernelWaitCond(cond, timeout);
}

static int heavy_signal(void) {
    return sceKernelSignalCond(cond);
}

static int heavy_delete_cond(void) {
    cond_gone = 1;
    return sceKernelDeleteCond(cond);
}

static int heavy_delete_mutex(void) {
    mutex_gone = 1;
    return sceKernelDeleteMutex(mutex);
}

static SceUID heavy_owner(void) {
    MutexInfo info = { .size = sizeof(info) };
    if (sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info) < 0)
        return 0;
    return info.currentOwnerId;
}

static SceUInt32 heavy_mutex_waiters(void) {
    MutexInfo info = { .size = sizeof(info) };
    if (sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info) < 0)
        return 0;
    return info.numWaitThreads;
}

static void lw_create(void) {
    lw_mutex = &lw_mutexes[lw_runs];
    lw_cond = &lw_conds[lw_runs];
    lw_runs++;
    sceKernelCreateLwMutex(lw_mutex, "lw mutex", 0, 0, NULL);
    sceKernelCreateLwCond(lw_cond, "lw cond", 0, lw_mutex, NULL);
    LwMutexInfo mutex_info = { .size = sizeof(mutex_info) };
    LwCondInfo cond_info = { .size = sizeof(cond_info) };
    sceKernelGetLwMutexInfo(lw_mutex, &mutex_info);
    sceKernelGetLwCondInfo(lw_cond, &cond_info);
    mutex_id = mutex_info.uid;
    cond_id = cond_info.uid;
    cond_gone = 0;
}

static void lw_destroy(void) {
    if (!cond_gone)
        sceKernelDeleteLwCond(lw_cond);
}

static int lw_lock(void) {
    SceUInt timeout = TEST_WAIT_US;
    return sceKernelLockLwMutex(lw_mutex, 1, &timeout);
}

static int lw_unlock(void) {
    return sceKernelUnlockLwMutex(lw_mutex, 1);
}

static int lw_try_lock(void) {
    return sceKernelTryLockLwMutex(lw_mutex, 1);
}

static int lw_wait(SceUInt *timeout) {
    return sceKernelWaitLwCond(lw_cond, timeout);
}

static int lw_signal(void) {
    return sceKernelSignalLwCond(lw_cond);
}

static int lw_delete_cond(void) {
    cond_gone = 1;
    return sceKernelDeleteLwCond(lw_cond);
}

static SceUID lw_owner(void) {
    LwMutexInfo info = { .size = sizeof(info) };
    if (sceKernelGetLwMutexInfo(lw_mutex, &info) < 0)
        return 0;
    return info.currentOwnerId;
}

static SceUInt32 lw_mutex_waiters(void) {
    LwMutexInfo info = { .size = sizeof(info) };
    if (sceKernelGetLwMutexInfo(lw_mutex, &info) < 0)
        return 0;
    return info.numWaitThreads;
}

static const Kind kinds[2] = {
    { "heavy", WAIT_TYPE_COND, WAIT_TYPE_COND_MUTEX, heavy_create, heavy_destroy, heavy_lock,
        heavy_unlock, heavy_try_lock, heavy_wait, heavy_signal, heavy_delete_cond, heavy_owner,
        heavy_mutex_waiters },
    { "lightweight", WAIT_TYPE_LW_COND, WAIT_TYPE_LW_COND_MUTEX, lw_create, lw_destroy, lw_lock,
        lw_unlock, lw_try_lock, lw_wait, lw_signal, lw_delete_cond, lw_owner, lw_mutex_waiters },
};

// Locks the mutex, waits on the condition, then records the result, the time left and the result of
// an unlock. W is suspended for every case until the main thread resumes it, so it records these
// only after the main thread has read the state.
static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    kind->lock();
    SceUInt timeout = COND_TIMEOUT_US;
    w_result = kind->wait(&timeout);
    w_left = (int)timeout;
    w_unlock = kind->unlock();
    return 0;
}

// Creates fresh objects and W, and returns once W waits on the condition
static SceUID start_w(const Kind *k) {
    kind = k;
    w_result = w_left = w_unlock = NOT_SET;
    k->create();
    const SceUID w = sceKernelCreateThread(
        "w", w_entry, WAITER_PRIORITY, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(w, k->cond_wait_type, cond_id));
    return w;
}

static SceKernelThreadInfo info_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info;
}

// Returns thread's info once it is neither READY nor RUNNING, for at most a second, so that a read
// doesn't land while a wake is still being applied to the thread.
static SceKernelThreadInfo settled_info(SceUID thread) {
    SceKernelThreadInfo info = info_of(thread);
    for (int i = 0; i < 1000 && (info.status & (SCE_THREAD_READY | SCE_THREAD_RUNNING)); i++) {
        sceKernelDelayThread(1000);
        info = info_of(thread);
    }
    return info;
}

// Polls, for at most 8 seconds, while thread is in a wait of the given type
static void wait_out_of(SceUID thread, SceUInt32 type) {
    for (int i = 0; i < 8000 && info_of(thread).waitType == type; i++)
        sceKernelDelayThread(1000);
}

// Polls, for at most 8 seconds, until thread is in a wait of the given type on id
static void wait_until_in(SceUID thread, SceUInt32 type, SceUID id) {
    for (int i = 0; i < 8000 && !test_in_wait(thread, type, id); i++)
        sceKernelDelayThread(1000);
}

// Waits for W to end, for at most 2 seconds, then deletes it. A W that doesn't end is left alone.
static void end_w(SceUID w) {
    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelWaitThreadEnd(w, NULL, &timeout) == 0)
        sceKernelDeleteThread(w);
}

// The main thread signals while W is suspended in its wait, holds the mutex, then unlocks it
static void signal_while_held(const Kind *k) {
    const char *const name = "signal while the main thread holds the mutex";
    const SceUID w = start_w(k);
    const int suspended = sceKernelSuspendThreadForVM(w);
    k->lock();
    k->signal();
    const SceKernelThreadInfo info = settled_info(w);
    const SceUInt32 waiters = k->mutex_waiters();
    k->unlock();
    const int owns = k->owner() == w;
    const int try_lock = k->try_lock();
    if (try_lock == 0)
        k->unlock();
    const SceUInt32 status_after_unlock = settled_info(w).status;
    const int resumed = sceKernelResumeThreadForVM(w);
    end_w(w);
    k->destroy();
    PRINTF(suspended, "%s, %s: suspend W", k->name, name);
    PRINTF(info.status, "%s, %s: W's status after the signal", k->name, name);
    PRINTF(info.waitType, "%s, %s: W's wait type after the signal", k->name, name);
    PRINTF(waiters, "%s, %s: the mutex's waiters after the signal", k->name, name);
    PRINTF(owns, "%s, %s: W owns the mutex after the unlock", k->name, name);
    PRINTF(try_lock, "%s, %s: try to lock the mutex after the unlock", k->name, name);
    PRINTF(status_after_unlock, "%s, %s: W's status after the unlock", k->name, name);
    PRINTF(resumed, "%s, %s: resume W", k->name, name);
    PRINTF(w_result, "%s, %s: W's result", k->name, name);
    PRINTF(w_unlock, "%s, %s: W's unlock", k->name, name);
}

// The main thread signals, waits until W reports the wait for the mutex, then suspends W and
// unlocks
static void signal_then_suspend_queued(const Kind *k) {
    const char *const name = "W queued for the mutex, then suspended before the unlock";
    const SceUID w = start_w(k);
    k->lock();
    k->signal();
    wait_until_in(w, k->mutex_wait_type, mutex_id);
    const int suspended = sceKernelSuspendThreadForVM(w);
    const SceKernelThreadInfo info = settled_info(w);
    const SceUInt32 waiters = k->mutex_waiters();
    k->unlock();
    const int owns = k->owner() == w;
    const int try_lock = k->try_lock();
    if (try_lock == 0)
        k->unlock();
    const SceUInt32 status_after_unlock = settled_info(w).status;
    const int resumed = sceKernelResumeThreadForVM(w);
    end_w(w);
    k->destroy();
    PRINTF(suspended, "%s, %s: suspend W", k->name, name);
    PRINTF(info.status, "%s, %s: W's status after the suspend", k->name, name);
    PRINTF(info.waitType, "%s, %s: W's wait type after the suspend", k->name, name);
    PRINTF(waiters, "%s, %s: the mutex's waiters after the suspend", k->name, name);
    PRINTF(owns, "%s, %s: W owns the mutex after the unlock", k->name, name);
    PRINTF(try_lock, "%s, %s: try to lock the mutex after the unlock", k->name, name);
    PRINTF(status_after_unlock, "%s, %s: W's status after the unlock", k->name, name);
    PRINTF(resumed, "%s, %s: resume W", k->name, name);
    PRINTF(w_result, "%s, %s: W's result", k->name, name);
    PRINTF(w_unlock, "%s, %s: W's unlock", k->name, name);
}

// W's wait times out while it is suspended. The main thread holds the mutex for the second run.
static void timeout_while_suspended(const Kind *k, int main_holds) {
    const char *const name = main_holds ? "timeout while suspended, the main thread holds the mutex"
                                        : "timeout while suspended, the mutex is free";
    const SceUID w = start_w(k);
    const int suspended = sceKernelSuspendThreadForVM(w);
    if (main_holds)
        k->lock();
    wait_out_of(w, k->cond_wait_type);
    const SceKernelThreadInfo info = settled_info(w);
    const int owns = k->owner() == w;
    const int try_lock = k->try_lock();
    if (try_lock == 0)
        k->unlock();
    if (main_holds)
        k->unlock();
    const int resumed = sceKernelResumeThreadForVM(w);
    end_w(w);
    k->destroy();
    PRINTF(suspended, "%s, %s: suspend W", k->name, name);
    PRINTF(info.status, "%s, %s: W's status after the timeout", k->name, name);
    PRINTF(info.waitType, "%s, %s: W's wait type after the timeout", k->name, name);
    PRINTF(owns, "%s, %s: W owns the mutex after the timeout", k->name, name);
    PRINTF(try_lock, "%s, %s: try to lock the mutex after the timeout", k->name, name);
    PRINTF(resumed, "%s, %s: resume W", k->name, name);
    PRINTF(w_result, "%s, %s: W's result", k->name, name);
    PRINTF(w_left == 0, "%s, %s: W's time left is 0", k->name, name);
    PRINTF(w_unlock, "%s, %s: W's unlock", k->name, name);
}

// The main thread deletes the condition while W is suspended in its wait
static void delete_cond_while_suspended(const Kind *k) {
    const char *const name = "delete the condition while W is suspended in its wait";
    const SceUID w = start_w(k);
    const int suspended = sceKernelSuspendThreadForVM(w);
    const int deleted = k->delete_cond();
    const SceKernelThreadInfo info = settled_info(w);
    const int resumed = sceKernelResumeThreadForVM(w);
    end_w(w);
    k->destroy();
    PRINTF(suspended, "%s, %s: suspend W", k->name, name);
    PRINTF(deleted, "%s, %s: delete the condition", k->name, name);
    PRINTF(info.status, "%s, %s: W's status after the delete", k->name, name);
    PRINTF(info.waitType, "%s, %s: W's wait type after the delete", k->name, name);
    PRINTF(resumed, "%s, %s: resume W", k->name, name);
    PRINTF(w_result, "%s, %s: W's result", k->name, name);
    PRINTF(w_unlock, "%s, %s: W's unlock", k->name, name);
}

// The main thread deletes the mutex while W is suspended in its wait, then resumes W and signals
static void delete_mutex_while_suspended(void) {
    const char *const name = "delete the mutex while W is suspended in its wait";
    const Kind *const k = &kinds[0];
    const SceUID w = start_w(k);
    const int suspended = sceKernelSuspendThreadForVM(w);
    const int deleted = heavy_delete_mutex();
    const SceKernelThreadInfo info = settled_info(w);
    const int resumed = sceKernelResumeThreadForVM(w);
    const int signaled = heavy_signal();
    end_w(w);
    k->destroy();
    PRINTF(suspended, "%s, %s: suspend W", k->name, name);
    PRINTF(deleted, "%s, %s: delete the mutex", k->name, name);
    PRINTF(info.status, "%s, %s: W's status after the delete", k->name, name);
    PRINTF(info.waitType, "%s, %s: W's wait type after the delete", k->name, name);
    PRINTF(resumed, "%s, %s: resume W", k->name, name);
    PRINTF(signaled, "%s, %s: signal the condition after the resume", k->name, name);
    PRINTF(w_result, "%s, %s: W's result", k->name, name);
    PRINTF(w_unlock, "%s, %s: W's unlock", k->name, name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    for (int i = 0; i < 2; i++) {
        signal_while_held(&kinds[i]);
        signal_then_suspend_queued(&kinds[i]);
        timeout_while_suspended(&kinds[i], 0);
        timeout_while_suspended(&kinds[i], 1);
        delete_cond_while_suspended(&kinds[i]);
    }
    delete_mutex_while_suspended();

    test_exit();
    return 0;
}
