// What a condition variable waiter W gets back when the thread that owns its mutex ends while W
// waits to lock the mutex again, after a signal or a timeout. Only a heavy mutex and condition are
// used. All the threads run on core 0, at the priorities main 0x60, owner 0xA0 and W 0xB0.
// The cases:
// - a: W waits on the condition, the owner locks the mutex and waits, the main thread signals and
//   waits until W reports the wait for the mutex, then the owner returns without unlocking. The
//   test prints W's result, whether W owns the mutex, its count and its waiters, W's unlock, and a
//   try lock after the unlock.
// - b: the same, but the owner locks the recursive mutex three times, so its end gives W the mutex
//   with the count the relock came with.
// - c: the same as a, but the owner ends with sceKernelExitThread in its entry function.
// - d: the main thread doesn't signal. W's condition wait times out while the owner still holds
//   the mutex, and W waits to lock it again. Then the owner returns, and the test reads what W got.
// W waits on a semaphore after it returns, while the main thread reads the mutex, so W's unlock
// happens after every read. Each wait in the test has a timeout, so a case that doesn't end can't
// stop the rest of the test.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define OWNER_PRIORITY 0xA0
#define WAITER_PRIORITY 0xB0
#define NOT_SET 0xDEAD
// Long enough for the owner to still hold the mutex when the condition timeout of case d passes
#define COND_TIMEOUT_US (2 * TEST_WAIT_US)
#define OWNER_HOLD_US (3 * TEST_WAIT_US)

// How a case runs
enum Case { RETURN_UNLOCKED, RECURSIVE_THREE, EXIT_THREAD, TIMED_OUT_FIRST };

static SceUID mutex, cond, go, go2;
static volatile int current, w_result, w_unlock;

// Takes the mutex, waits for go, then ends without giving the mutex back
static int owner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt lock_timeout = TEST_WAIT_US;
    sceKernelLockMutex(mutex, current == RECURSIVE_THREE ? 3 : 1, &lock_timeout);
    SceUInt timeout = OWNER_HOLD_US;
    sceKernelWaitSema(go, 1, &timeout);
    if (current == EXIT_THREAD)
        sceKernelExitThread(0);
    return 0;
}

// Waits on the condition, records the result, then waits for go2 before it unlocks the mutex
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt lock_timeout = TEST_WAIT_US;
    sceKernelLockMutex(mutex, 1, &lock_timeout);
    SceUInt timeout = COND_TIMEOUT_US;
    w_result = sceKernelWaitCond(cond, &timeout);
    SceUInt gate_timeout = OWNER_HOLD_US;
    sceKernelWaitSema(go2, 1, &gate_timeout);
    w_unlock = sceKernelUnlockMutex(mutex, 1);
    return 0;
}

// Polls, for at most 8 seconds, until thread is in a wait of the given type on id
static void wait_until_in(SceUID thread, SceUInt32 type, SceUID id) {
    for (int i = 0; i < 8000 && !test_in_wait(thread, type, id); i++)
        sceKernelDelayThread(1000);
}

static void run_case(const char *name, int new_case) {
    current = new_case;
    w_result = w_unlock = NOT_SET;
    mutex = sceKernelCreateMutex(
        "mutex", current == RECURSIVE_THREE ? SCE_KERNEL_MUTEX_ATTR_RECURSIVE : 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    go2 = sceKernelCreateSema("go2", 0, 0, 1, NULL);

    const SceUID w = sceKernelCreateThread(
        "waiter", waiter_entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(w, WAIT_TYPE_COND, cond));

    const SceUID owner = sceKernelCreateThread(
        "owner", owner_entry, OWNER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(owner, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(owner, WAIT_TYPE_SEMA, go));

    if (current != TIMED_OUT_FIRST)
        sceKernelSignalCond(cond);
    // With the timeout of case d, W's wait ends and it queues for the mutex the owner holds
    wait_until_in(w, WAIT_TYPE_COND_MUTEX, mutex);
    const int relocking = test_in_wait(w, WAIT_TYPE_COND_MUTEX, mutex);

    sceKernelSignalSema(go, 1);
    SceUInt owner_timeout = TEST_WAIT_US;
    const int owner_ended = sceKernelWaitThreadEnd(owner, NULL, &owner_timeout) == 0;
    if (owner_ended)
        sceKernelDeleteThread(owner);

    // W is ready after the owner's end, and does nothing until this thread blocks
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    const int owns = info.currentOwnerId == w;
    const SceInt32 count = info.currentCount;
    const SceUInt32 waiters = info.numWaitThreads;

    // W parks on go2 once it has run, and only then unlocks the mutex
    wait_until_in(w, WAIT_TYPE_SEMA, go2);
    const int parked = test_in_wait(w, WAIT_TYPE_SEMA, go2);
    sceKernelSignalSema(go2, 1);
    SceUInt waiter_timeout = TEST_WAIT_US;
    if (sceKernelWaitThreadEnd(w, NULL, &waiter_timeout) == 0)
        sceKernelDeleteThread(w);
    const int try_lock = sceKernelTryLockMutex(mutex, 1);
    if (try_lock == 0)
        sceKernelUnlockMutex(mutex, 1);

    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(mutex);
    sceKernelDeleteSema(go);
    sceKernelDeleteSema(go2);

    PRINTF(owner_ended, "%s: the owner ended", name);
    PRINTF(relocking, "%s: W reports the wait for the mutex", name);
    PRINTF(parked, "%s: W got to its gate after the owner's end", name);
    PRINTF(owns, "%s: W owns the mutex", name);
    PRINTF(count, "%s: the mutex's count", name);
    PRINTF(waiters, "%s: the mutex's waiters", name);
    PRINTF(w_result, "%s: W's result", name);
    PRINTF(w_unlock, "%s: W's unlock", name);
    PRINTF(try_lock, "%s: try to lock the mutex after the unlock", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("the owner returns, W signaled", RETURN_UNLOCKED);
    run_case("recursive mutex held 3 times, the owner returns", RECURSIVE_THREE);
    run_case("the owner calls sceKernelExitThread", EXIT_THREAD);
    run_case("W's timeout passes first, the owner returns", TIMED_OUT_FIRST);

    test_exit();
    return 0;
}
