// Whether the relock of a condition variable wait has no deadline when the wait is restarted after
// a suspend. W, a thread with the VM attribute on core 0, waits on a condition variable with a
// timeout. The main thread, on core 1, suspends W with sceKernelSuspendThreadForVM and takes the
// mutex. W's wait times out while it is suspended, and the main thread polls until the wait is over.
// It resumes W while it still holds the mutex, so W has to relock it. The main thread keeps the
// mutex for three times the timeout, then reads W. A relock with the timeout of the wait would be
// over by then, and one with no deadline still waits. The main thread unlocks and reads W's result.
// Every wait has a bound, so a case that doesn't end can't stop the test. It runs for the heavy
// condition variable (sceKernelCreateCond) and the lightweight one (sceKernelCreateLwCond).
//
// On a PS Vita TV on firmware 3.65 both variants show the same. The suspend returns 1 and W shows
// SUSPENDED 0x100 with wait type 0 once its wait has timed out. After the resume W waits for the
// mutex (wait type 0x200 for the heavy variant and 0x400000 for the lightweight one, status 0x8)
// and still does so after three times the timeout, with 1 waiter on the mutex and no result set.
// After the unlock the cond wait returns 0 with a time left of 0 and W's own unlock returns 0, so
// W owned the mutex again. The relock has no deadline.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define WAITER_PRIORITY 0xA0
#define COND_TIMEOUT_US 500000
#define HOLD_US (3 * COND_TIMEOUT_US)
#define NOT_SET 0xDEAD

typedef enum Variant { HEAVY,
    LIGHT } Variant;

static SceUID mutex, cond;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static Variant variant;
// The UID a wait on the mutex reports
static SceUID mutex_id;
static volatile int w_result, w_left, w_unlock;

static int lock_mutex(void) {
    SceUInt timeout = TEST_WAIT_US;
    return variant == HEAVY ? sceKernelLockMutex(mutex, 1, &timeout)
                            : sceKernelLockLwMutex(&lw_mutex, 1, &timeout);
}

static int unlock_mutex(void) {
    return variant == HEAVY ? sceKernelUnlockMutex(mutex, 1)
                            : sceKernelUnlockLwMutex(&lw_mutex, 1);
}

static SceUInt32 mutex_waiters(void) {
    if (variant == HEAVY) {
        MutexInfo info = { .size = sizeof(info) };
        return sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info) < 0 ? 0 : info.numWaitThreads;
    }
    LwMutexInfo info = { .size = sizeof(info) };
    return sceKernelGetLwMutexInfo(&lw_mutex, &info) < 0 ? 0 : info.numWaitThreads;
}

// Locks the mutex, waits on the condition variable, then records the result, the time left and the
// result of an unlock, which is 0 only if W owned the mutex again
static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    lock_mutex();
    SceUInt timeout = COND_TIMEOUT_US;
    w_result = variant == HEAVY ? sceKernelWaitCond(cond, &timeout)
                                : sceKernelWaitLwCond(&lw_cond, &timeout);
    w_left = (int)timeout;
    w_unlock = unlock_mutex();
    return 0;
}

static SceKernelThreadInfo info_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info;
}

// Polls, for at most 8 seconds, while thread is in a wait of the given type
static void wait_out_of(SceUID thread, SceUInt32 type) {
    for (int i = 0; i < 8000 && info_of(thread).waitType == type; i++)
        sceKernelDelayThread(1000);
}

// Polls, for at most 2 seconds, until thread has run its kernel tail and stopped
static void wait_until_settled(SceUID thread) {
    for (int i = 0; i < 2000 && (info_of(thread).status & (SCE_THREAD_RUNNING | SCE_THREAD_READY)); i++)
        sceKernelDelayThread(1000);
}

// Polls, for at most 2 seconds, until thread is in a wait of the given type on id
static void wait_until_in(SceUID thread, SceUInt32 type, SceUID id) {
    for (int i = 0; i < 2000 && !test_in_wait(thread, type, id); i++)
        sceKernelDelayThread(1000);
}

static void run_case(Variant v, const char *name) {
    variant = v;
    w_result = w_left = w_unlock = NOT_SET;
    SceUID cond_id;
    SceUInt32 cond_type, mutex_type;
    if (v == HEAVY) {
        mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
        cond = sceKernelCreateCond("cond", 0, mutex, NULL);
        mutex_id = mutex;
        cond_id = cond;
        cond_type = WAIT_TYPE_COND;
        mutex_type = WAIT_TYPE_COND_MUTEX;
    } else {
        sceKernelCreateLwMutex(&lw_mutex, "mutex", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "cond", 0, &lw_mutex, NULL);
        LwMutexInfo mutex_info = { .size = sizeof(mutex_info) };
        LwCondInfo cond_info = { .size = sizeof(cond_info) };
        sceKernelGetLwMutexInfo(&lw_mutex, &mutex_info);
        sceKernelGetLwCondInfo(&lw_cond, &cond_info);
        mutex_id = mutex_info.uid;
        cond_id = cond_info.uid;
        cond_type = WAIT_TYPE_LW_COND;
        mutex_type = WAIT_TYPE_LW_COND_MUTEX;
    }

    const SceUID w = sceKernelCreateThread(
        "w", w_entry, WAITER_PRIORITY, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(w, cond_type, cond_id));
    const int suspended = sceKernelSuspendThreadForVM(w);
    lock_mutex();
    wait_out_of(w, cond_type);
    wait_until_settled(w);
    const SceKernelThreadInfo before = info_of(w);
    const int resumed = sceKernelResumeThreadForVM(w);
    wait_until_in(w, mutex_type, mutex_id);
    const int in_relock_at_once = test_in_wait(w, mutex_type, mutex_id);
    sceKernelDelayThread(HOLD_US);
    const SceKernelThreadInfo after = info_of(w);
    const int in_relock_later = test_in_wait(w, mutex_type, mutex_id);
    const SceUInt32 waiters = mutex_waiters();
    const int result_before_unlock = w_result;
    const int unlocked = unlock_mutex();

    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelWaitThreadEnd(w, NULL, &timeout) == 0)
        sceKernelDeleteThread(w);
    if (v == HEAVY) {
        sceKernelDeleteCond(cond);
        sceKernelDeleteMutex(mutex);
    } else {
        sceKernelDeleteLwCond(&lw_cond);
        sceKernelDeleteLwMutex(&lw_mutex);
    }

    PRINTF(suspended, "%s: suspend W", name);
    PRINTF(before.status, "%s: W's status after the timeout, suspended", name);
    PRINTF(before.waitType, "%s: W's wait type after the timeout, suspended", name);
    PRINTF(resumed, "%s: resume W while the main thread holds the mutex", name);
    PRINTF(in_relock_at_once, "%s: W waits for the mutex after the resume", name);
    PRINTF(after.status, "%s: W's status after three times the timeout", name);
    PRINTF(after.waitType, "%s: W's wait type after three times the timeout", name);
    PRINTF(in_relock_later, "%s: W still waits for the mutex", name);
    PRINTF(waiters, "%s: the mutex's waiters", name);
    PRINTF(result_before_unlock == NOT_SET, "%s: W has no result before the unlock", name);
    PRINTF(unlocked, "%s: unlock the mutex", name);
    PRINTF(w_result, "%s: W's result", name);
    PRINTF(w_left == 0, "%s: W's time left is 0", name);
    PRINTF(w_unlock, "%s: W's unlock", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case(HEAVY, "heavy");
    run_case(LIGHT, "lightweight");

    test_exit();
    return 0;
}
