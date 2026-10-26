// Cancels a mutex while a condition waiter waits to lock it again after a signal. A thread W waits
// on a condition with its mutex. The main thread locks the mutex, signals the condition and waits
// until W reports the relocking wait type, 0x200. Then it cancels the mutex, in case a to a count
// of 1, which keeps the mutex held by the main thread, and in case b to 0, which releases it. W's
// wait ends with the cancel and W waits at a gate, holding whatever it got, so the test reads the
// mutex while W is blocked. The test prints W's wait type before the cancel, the cancel's result
// and the waiting thread count it wrote, W's wait result, whether W owns the mutex after its wait,
// and the mutex's count after the cancel. Then it lets W give back the mutex and prints W's unlock.
// The model cond_mutex_delete covers the relock wait and cancel_calls the cancel of a mutex with
// waiters.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID mutex, cond, gate;
static volatile int waiter_result = 0xDEAD, waiter_unlock = 0xDEAD;

// Waits on the condition, then waits at the gate holding what the wait left, and unlocks the mutex
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelLockMutex(mutex, 1, &timeout);
    timeout = TEST_WAIT_US;
    waiter_result = sceKernelWaitCond(cond, &timeout);
    timeout = TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    waiter_unlock = sceKernelUnlockMutex(mutex, 1);
    return 0;
}

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

static MutexInfo mutex_info(void) {
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info;
}

static void run_case(const char *name, int count) {
    waiter_result = waiter_unlock = 0xDEAD;
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);

    const SceUID w = test_start_thread(waiter);
    // W releases the mutex while it waits, so the main thread can lock it for the signal
    TEST_WAIT_FOR(wait_type_of(w) == WAIT_TYPE_COND);
    SceUInt32 lock_timeout = TEST_WAIT_US;
    sceKernelLockMutex(mutex, 1, &lock_timeout);
    sceKernelSignalCond(cond);
    TEST_WAIT_FOR(wait_type_of(w) == WAIT_TYPE_COND_MUTEX);
    PRINTF(wait_type_of(w), "%s: W's wait type before the cancel", name);

    int waiting = 0xDEAD;
    const int cancel = sceKernelCancelMutex(mutex, count, &waiting);
    // The cancel ends W's wait for the mutex, and W then waits at the gate
    TEST_WAIT_FOR(wait_type_of(w) == WAIT_TYPE_SEMA);
    const MutexInfo info = mutex_info();
    PRINTF(cancel, "%s: the cancel to a count of %d", name, count);
    PRINTF(waiting, "%s: the waiting threads the cancel wrote", name);
    PRINTF(waiter_result, "%s: W's wait result", name);
    PRINTF(info.currentOwnerId == w, "%s: W owns the mutex after its wait", name);
    PRINTF(info.currentCount, "%s: the mutex's count after the cancel", name);

    sceKernelSignalSema(gate, 1);
    test_join_thread(w);
    PRINTF(waiter_unlock, "%s: W's unlock after it", name);
    if (count > 0)
        sceKernelUnlockMutex(mutex, 1);

    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(mutex);
    sceKernelDeleteSema(gate);
}

int main(void) {
    run_case("case a", 1);
    run_case("case b", 0);

    test_exit();
    return 0;
}
