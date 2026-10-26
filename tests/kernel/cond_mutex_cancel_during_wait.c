// Cancels a heavy mutex while a condition waiter W waits on the condition with it. The test checks
// that the cancel leaves the condition's waiters alone, and what state the mutex is in when W
// relocks it after a signal. A recursive mutex that W locked twice is owned by W during its wait,
// and the test prints the count it has then. Every thread is pinned to core 0, main runs at 0x60
// and W at 0xB0. Each case uses fresh objects and reads the state only while W is blocked in a
// wait, which is before the signal and after W reports its relock. Cases:
// - a: a plain mutex that W released by its wait. Main cancels it to 0, which leaves it free, then
//   signals, and W relocks it
// - b: the same, but main cancels it to 1, so main owns it. Main waits for W's relock report (wait
//   type 0x200), then unlocks, and W gets the mutex
// - c: a recursive mutex that W locked twice. Main cancels it to 0, which frees it from W, then
//   signals, and W relocks it and unlocks it twice
// - d: the same as c, but main cancels it to 1, so main owns it. Main waits for W's report, then
//   unlocks, and W gets the mutex with the count its relock brings
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0

static SceUID mutex, cond, waiter;
static volatile int w_result, w_count, w_unlock1, w_unlock2;

// Locks the plain mutex, waits on the condition, then unlocks it
static int plain_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    SceUInt timeout = TEST_WAIT_US;
    w_result = sceKernelWaitCond(cond, &timeout);
    w_unlock1 = sceKernelUnlockMutex(mutex, 1);
    return 0;
}

// Locks the recursive mutex twice, waits on the condition, then records the count it has and
// unlocks it twice
static int recursive_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 2, NULL);
    SceUInt timeout = TEST_WAIT_US;
    w_result = sceKernelWaitCond(cond, &timeout);
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    w_count = info.currentCount;
    w_unlock1 = sceKernelUnlockMutex(mutex, 1);
    w_unlock2 = sceKernelUnlockMutex(mutex, 1);
    return 0;
}

static MutexInfo mutex_info(void) {
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info;
}

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

static int cond_waiters(void) {
    SceKernelCondInfo info = { .size = sizeof(info) };
    sceKernelGetCondInfo(cond, &info);
    return info.numWaitThreads;
}

static void create(SceUInt32 mutex_attr) {
    mutex = sceKernelCreateMutex("mutex", mutex_attr, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    w_result = w_count = w_unlock1 = w_unlock2 = 0xDEAD;
}

static void destroy(void) {
    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(mutex);
}

// Waits up to 4 s for a thread to end and deletes it. A relock has no timeout, so a thread that is
// still running ends the test with a marker line, instead of hanging it.
static void join_or_exit(SceUID thread) {
    SceUInt timeout = 2 * TEST_WAIT_US;
    if (sceKernelWaitThreadEnd(thread, NULL, &timeout) < 0) {
        PRINT("a thread did not end within 4 s", 1);
        test_exit();
    }
    sceKernelDeleteThread(thread);
}

// Starts W at the waiter priority, and returns once it waits on the condition
static SceUID start_waiter(SceKernelThreadEntry entry) {
    const SceUID thread = sceKernelCreateThread("waiter", entry, WAITER_PRIORITY, 0x4000, 0,
                                                SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_COND, cond));
    return thread;
}

// The mutex stays free: main cancels it to 0 while W waits, then signals and W relocks it
static void cancel_free_then_signal(void) {
    create(0);
    waiter = start_waiter(plain_waiter);
    int waiting = 0xDEAD;
    const int cancel = sceKernelCancelMutex(mutex, 0, &waiting);
    const SceUInt32 wait_type = wait_type_of(waiter);
    const int waiters = cond_waiters();
    const int owned = mutex_info().currentOwnerId != 0;
    sceKernelSignalCond(cond);
    join_or_exit(waiter);
    PRINTF(cancel, "a: the cancel to 0");
    PRINTF(waiting, "a: the waiting threads the cancel wrote");
    PRINTF(wait_type, "a: W's wait type after the cancel");
    PRINTF(waiters, "a: the condition's waiters after the cancel");
    PRINTF(owned, "a: the mutex has an owner after the cancel");
    PRINTF(w_result, "a: W's wait result");
    PRINTF(w_unlock1, "a: W's unlock");
    destroy();
}

// Main cancels the mutex to 1, so it owns it while W waits. Main unlocks after W reports the relock
static void cancel_held_then_signal(void) {
    create(0);
    waiter = start_waiter(plain_waiter);
    int waiting = 0xDEAD;
    const int cancel = sceKernelCancelMutex(mutex, 1, &waiting);
    const SceUInt32 wait_type = wait_type_of(waiter);
    const int waiters = cond_waiters();
    const int owns = mutex_info().currentOwnerId == sceKernelGetThreadId();
    const int count = mutex_info().currentCount;
    sceKernelSignalCond(cond);
    TEST_WAIT_FOR(test_in_wait(waiter, WAIT_TYPE_COND_MUTEX, mutex));
    const SceUInt32 reported = wait_type_of(waiter);
    sceKernelUnlockMutex(mutex, 1);
    join_or_exit(waiter);
    PRINTF(cancel, "b: the cancel to 1");
    PRINTF(waiting, "b: the waiting threads the cancel wrote");
    PRINTF(wait_type, "b: W's wait type after the cancel");
    PRINTF(waiters, "b: the condition's waiters after the cancel");
    PRINTF(owns, "b: main owns the mutex after the cancel");
    PRINTF(count, "b: the count after the cancel");
    PRINTF(reported, "b: W's wait type after the signal");
    PRINTF(w_result, "b: W's wait result");
    PRINTF(w_unlock1, "b: W's unlock");
    destroy();
}

// W holds the recursive mutex twice while it waits. Main cancels it to 0, which frees it from W
static void recursive_cancel_free_then_signal(void) {
    create(SCE_KERNEL_MUTEX_ATTR_RECURSIVE);
    waiter = start_waiter(recursive_waiter);
    const int owned_by_w = mutex_info().currentOwnerId == waiter;
    const int count_in_wait = mutex_info().currentCount;
    int waiting = 0xDEAD;
    const int cancel = sceKernelCancelMutex(mutex, 0, &waiting);
    const int owner_after = mutex_info().currentOwnerId;
    const int count_after = mutex_info().currentCount;
    sceKernelSignalCond(cond);
    join_or_exit(waiter);
    PRINTF(owned_by_w, "c: W owns the recursive mutex during its wait");
    PRINTF(count_in_wait, "c: the count during the wait");
    PRINTF(cancel, "c: the cancel to 0");
    PRINTF(waiting, "c: the waiting threads the cancel wrote");
    PRINTF(owner_after == 0, "c: the mutex has no owner after the cancel");
    PRINTF(count_after, "c: the count after the cancel");
    PRINTF(w_result, "c: W's wait result");
    PRINTF(w_count, "c: the count W has after its relock");
    PRINTF(w_unlock1, "c: W's first unlock");
    PRINTF(w_unlock2, "c: W's second unlock");
    destroy();
}

// W holds the recursive mutex twice while it waits. Main cancels it to 1, so main owns it, then
// unlocks after W reports the relock
static void recursive_cancel_held_then_signal(void) {
    create(SCE_KERNEL_MUTEX_ATTR_RECURSIVE);
    waiter = start_waiter(recursive_waiter);
    const int owned_by_w = mutex_info().currentOwnerId == waiter;
    const int count_in_wait = mutex_info().currentCount;
    int waiting = 0xDEAD;
    const int cancel = sceKernelCancelMutex(mutex, 1, &waiting);
    const int owned_by_main = mutex_info().currentOwnerId == sceKernelGetThreadId();
    const int count_after = mutex_info().currentCount;
    sceKernelSignalCond(cond);
    TEST_WAIT_FOR(test_in_wait(waiter, WAIT_TYPE_COND_MUTEX, mutex));
    const SceUInt32 reported = wait_type_of(waiter);
    sceKernelUnlockMutex(mutex, 1);
    join_or_exit(waiter);
    PRINTF(owned_by_w, "d: W owns the recursive mutex during its wait");
    PRINTF(count_in_wait, "d: the count during the wait");
    PRINTF(cancel, "d: the cancel to 1");
    PRINTF(waiting, "d: the waiting threads the cancel wrote");
    PRINTF(owned_by_main, "d: main owns the mutex after the cancel");
    PRINTF(count_after, "d: the count after the cancel");
    PRINTF(reported, "d: W's wait type after the signal");
    PRINTF(w_result, "d: W's wait result");
    PRINTF(w_count, "d: the count W has after its relock");
    PRINTF(w_unlock1, "d: W's first unlock");
    PRINTF(w_unlock2, "d: W's second unlock");
    destroy();
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    cancel_free_then_signal();
    cancel_held_then_signal();
    recursive_cancel_free_then_signal();
    recursive_cancel_held_then_signal();

    test_exit();
    return 0;
}
