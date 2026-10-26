// Whether a release gives the object to a waiting thread before that thread
// runs, or leaves it for whoever takes it first, for the kinds wake_handoff
// leaves out: a heavy mutex with count 1 that is not recursive, an auto reset
// timer, a condition variable signal and a lightweight condition signal.
// wake_handoff covers the semaphore, event flag, lightweight mutex, rwlock,
// simple event and message pipe, and queue_order covers the heavy mutex's
// queue. Every thread is pinned to core 0 and the waiters run below the main
// thread, so a waiter can't run between the release and the try. For the mutex
// kind the main thread unlocks, then tries to lock right back. For the
// condition kinds the waiter is signaled into the wait for its mutex, so the
// release is the signal and the unlock of the mutex that follows it, and the
// try takes that mutex back. The timer's release is its own event, at its time,
// so the main thread spins on a poll of the timer until the poll gets the event
// or one second passes, which is after the event time of 300 ms. Each value is
// read first and printed after the waiter ended, since printing lets the waiter
// run.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0

// The timer's event time, and how long the main thread spins on its poll
#define TIMER_EVENT_US 300000
#define SPIN_US 1000000

typedef enum Kind { MUTEX, TIMER, COND, LW_COND } Kind;

static Kind kind;
static SceUID object, cond_mutex;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static volatile int result;
static SceUInt32 bits;

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    switch (kind) {
    case MUTEX:
        result = sceKernelLockMutex(object, 1, &timeout);
        if (result == 0)
            sceKernelUnlockMutex(object, 1);
        break;
    case TIMER:
        result = sceKernelWaitEvent(object, EVENT_TIMER, &bits, NULL, &timeout);
        break;
    case COND:
        sceKernelLockMutex(cond_mutex, 1, NULL);
        result = sceKernelWaitCond(object, &timeout);
        sceKernelUnlockMutex(cond_mutex, 1);
        break;
    case LW_COND:
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        result = sceKernelWaitLwCond(&lw_cond, &timeout);
        sceKernelUnlockLwMutex(&lw_mutex, 1);
        break;
    }
    return 0;
}

static SceUID start_waiter(Kind k) {
    kind = k;
    result = 0xDEAD;
    bits = 0xDEAD;
    const SceUID thread = sceKernelCreateThread("waiter", waiter, WAITER_PRIORITY, 0x4000, 0,
                                                SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    test_wait_until_waiting(thread);
    return thread;
}

// Whether thread still reports a wait
static int still_waits(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return (info.status & SCE_THREAD_WAITING) != 0;
}

static void mutex_case(void) {
    object = sceKernelCreateMutex("mutex", 0, 1, NULL);
    const SceUID thread = start_waiter(MUTEX);
    sceKernelUnlockMutex(object, 1);
    const int waits = still_waits(thread);
    const int try_lock = sceKernelTryLockMutex(object, 1);
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(object, (SceKernelMutexInfo *)&info);
    if (try_lock == 0)
        sceKernelUnlockMutex(object, 1);
    test_join_thread(thread);
    PRINT("mutex: the waiter still reports its wait after the unlock", waits);
    PRINT("mutex: the unlocker's try lock right after it", try_lock);
    PRINT("mutex: the owner after the try is the waiter", info.currentOwnerId == thread);
    PRINT("mutex: the count after the try", info.currentCount);
    PRINT("mutex: the waiters after the try", info.numWaitThreads);
    PRINT("mutex: the waiter's result", result);
    sceKernelDeleteMutex(object);
}

static void timer_case(void) {
    object = sceKernelCreateTimer("timer", EVENT_ATTR_AUTO_RESET, NULL);
    SceUInt64 interval = TIMER_EVENT_US;
    sceKernelSetTimerEvent(object, TIMER_TYPE_SET_EVENT, &interval, 0);
    const SceUID thread = start_waiter(TIMER);
    sceKernelStartTimer(object);
    // The main thread keeps the core until the event time has passed, so the
    // waiter can't run
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    SceUInt32 polled = 0xDEAD;
    SceUInt64 user_data = 0;
    int poll = 0;
    do {
        poll = sceKernelPollEvent(object, EVENT_TIMER, &polled, &user_data);
    } while (poll != 0 && sceKernelGetProcessTimeWide() - start < SPIN_US);
    const int waits = still_waits(thread);
    test_join_thread(thread);
    PRINT("timer: the waiter still reports its wait after the event time", waits);
    PRINT("timer: the releasing thread's poll after the event time", poll);
    PRINT("timer: the bits that poll got", poll == 0 ? polled : 0);
    PRINT("timer: the waiter's result", result);
    PRINT("timer: the bits the waiter got", bits);
    sceKernelStopTimer(object);
    sceKernelDeleteTimer(object);
}

static void cond_case(void) {
    cond_mutex = sceKernelCreateMutex("cond mutex", 0, 0, NULL);
    object = sceKernelCreateCond("cond", 0, cond_mutex, NULL);
    const SceUID thread = start_waiter(COND);
    sceKernelLockMutex(cond_mutex, 1, NULL);
    sceKernelSignalCond(object);
    sceKernelUnlockMutex(cond_mutex, 1);
    const int waits = still_waits(thread);
    const int try_lock = sceKernelTryLockMutex(cond_mutex, 1);
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(cond_mutex, (SceKernelMutexInfo *)&info);
    if (try_lock == 0)
        sceKernelUnlockMutex(cond_mutex, 1);
    test_join_thread(thread);
    PRINT("condition: the waiter still reports its wait after the signal and "
          "unlock",
          waits);
    PRINT("condition: the unlocker's try lock right after it", try_lock);
    PRINT("condition: the owner of the mutex after the try is the waiter",
          info.currentOwnerId == thread);
    PRINT("condition: the count after the try", info.currentCount);
    PRINT("condition: the waiters for the mutex after the try", info.numWaitThreads);
    PRINT("condition: the waiter's result", result);
    sceKernelDeleteCond(object);
    sceKernelDeleteMutex(cond_mutex);
}

static void lw_cond_case(void) {
    sceKernelCreateLwMutex(&lw_mutex, "lw", 0, 0, NULL);
    sceKernelCreateLwCond(&lw_cond, "lwcond", 0, &lw_mutex, NULL);
    const SceUID thread = start_waiter(LW_COND);
    sceKernelLockLwMutex(&lw_mutex, 1, NULL);
    sceKernelSignalLwCond(&lw_cond);
    sceKernelUnlockLwMutex(&lw_mutex, 1);
    const int waits = still_waits(thread);
    const int try_lock = sceKernelTryLockLwMutex(&lw_mutex, 1);
    LwMutexInfo info = { .size = sizeof(info) };
    sceKernelGetLwMutexInfo(&lw_mutex, &info);
    if (try_lock == 0)
        sceKernelUnlockLwMutex(&lw_mutex, 1);
    test_join_thread(thread);
    PRINT("lightweight condition: the waiter still reports its wait after the "
          "signal and unlock",
          waits);
    PRINT("lightweight condition: the unlocker's try lock right after it", try_lock);
    PRINT("lightweight condition: the owner of the mutex after the try is the "
          "waiter",
          info.currentOwnerId == thread);
    PRINT("lightweight condition: the count after the try", info.currentCount);
    PRINT("lightweight condition: the waiters for the mutex after the try", info.numWaitThreads);
    PRINT("lightweight condition: the waiter's result", result);
    sceKernelDeleteLwCond(&lw_cond);
    sceKernelDeleteLwMutex(&lw_mutex);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    mutex_case();
    timer_case();
    cond_case();
    lw_cond_case();

    test_exit();
    return 0;
}
