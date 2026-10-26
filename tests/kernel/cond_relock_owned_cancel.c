// A condition variable waiter whose callback takes its mutex after the signal relocks a mutex it
// owns. Thread T locks a heavy mutex M and waits on a heavy condition C with callbacks and a
// timeout of 8 s, which releases M. The main thread locks M and signals C, so T waits to lock M
// again, then it notifies T's callback. The callback blocks on a gate with a timeout of 3 s, and
// locks M once the gate opens. The main thread unlocks M while the callback is blocked, so nothing
// holds M meanwhile, and then opens the gate. The relock has no timeout, so the main thread polls
// up to 5 s for T to wait for M with its callback's lock held, then cancels M to 0 on every path,
// which is the only call here that can end that wait. The test prints each call's result, whether
// the wait showed, whether T owns M while it waits, the number of waiters the cancel woke, T's
// result, whether T owns M after the wait, the results of T's unlock and of
// sceKernelCheckCallback, and the runs of the callback. It runs last in its own test, since a
// wrong implementation can wait forever on the relock.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define WAIT_US 8000000
#define GATE_US 3000000
#define NOT_DONE 0xDEAD

static SceUID mutex, cond, callback, gate;
static volatile int callback_ready, callback_runs, callback_lock, t_done;
static volatile int t_result, t_owns, t_unlock, t_check;

// Blocks on the gate, then locks M, which the main thread unlocked while the callback waited
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    SceUInt timeout = GATE_US;
    sceKernelWaitSema(gate, 1, &timeout);
    callback_lock = sceKernelLockMutex(mutex, 1, NULL);
    return 0;
}

// Returns the owner of the mutex
static SceUID mutex_owner(void) {
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info.currentOwnerId;
}

// Returns the number of threads waiting for the mutex
static int mutex_waiters(void) {
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info.numWaitThreads;
}

// Locks M, waits on C with callbacks, then records how the wait ended and whether T owns M
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    sceKernelLockMutex(mutex, 1, NULL);
    callback_ready = 1;
    SceUInt timeout = WAIT_US;
    t_result = sceKernelWaitCondCB(cond, &timeout);
    t_owns = mutex_owner() == sceKernelGetThreadId();
    t_unlock = sceKernelUnlockMutex(mutex, 1);
    t_check = sceKernelCheckCallback();
    sceKernelDeleteCallback(callback);
    t_done = 1;
    return 0;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    callback_ready = callback_runs = callback_lock = t_done = 0;
    t_result = t_owns = t_unlock = t_check = NOT_DONE;

    const SceUID t = sceKernelCreateThread("t", t_entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(callback_ready && test_in_wait(t, WAIT_TYPE_COND | WAIT_TYPE_CB, cond));
    sceKernelLockMutex(mutex, 1, NULL);
    const int signal = sceKernelSignalCond(cond);
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_COND_MUTEX | WAIT_TYPE_CB, mutex));
    const int notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, gate));
    const int unlock = sceKernelUnlockMutex(mutex, 1);
    sceKernelSignalSema(gate, 1);

    // Up to 5 s for the callback to take M and T to wait for it. Only the cancel below ends it.
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_COND_MUTEX | WAIT_TYPE_CB, mutex));
    const int showed = test_in_wait(t, WAIT_TYPE_COND_MUTEX | WAIT_TYPE_CB, mutex);
    const int owned_by_t = mutex_owner() == t;
    const int waiters = mutex_waiters();
    int woken = NOT_DONE;
    const int cancel = sceKernelCancelMutex(mutex, 0, &woken);
    TEST_WAIT_FOR(t_done);
    SceUInt join_timeout = 5 * TEST_WAIT_US;
    if (sceKernelWaitThreadEnd(t, NULL, &join_timeout) == 0)
        sceKernelDeleteThread(t);
    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(mutex);
    sceKernelDeleteSema(gate);

    PRINTF(signal, "the signal");
    PRINTF(notify, "the notify");
    PRINTF(unlock, "the main thread's unlock while the callback waits");
    PRINTF(showed, "T waits for the mutex with its callback taken");
    PRINTF(owned_by_t, "the mutex's owner is T while it waits");
    PRINTF(waiters, "the mutex's waiters while T waits");
    PRINTF(callback_lock, "the callback's lock");
    PRINTF(callback_runs, "runs of the callback");
    PRINTF(cancel, "the cancel to 0");
    PRINTF(woken, "the waiters the cancel woke");
    PRINTF(t_result, "T's wait");
    PRINTF(t_owns, "T owns the mutex after the wait");
    PRINTF(t_unlock, "T's unlock");
    PRINTF(t_check, "T's check");

    test_exit();
    return 0;
}
