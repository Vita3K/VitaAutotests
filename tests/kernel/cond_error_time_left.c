// What the timeout variable of a condition variable wait holds after a call that fails before it
// arms a timer, and after a mutex delete ends a wait. The variable starts at 1.5 s, and each result
// prints whether the time left is unchanged, 0, or between 0 and the preset. wait_zero_timeout
// and delete_with_waiters cover timeouts of 0 and the time left of a condition delete, not these.
// The cases, each for the heavy and the lightweight condition variable unless noted:
// - the caller doesn't own the mutex, which is free
// - an unknown condition variable, and a deleted one
// - a condition variable whose mutex was deleted, heavy only
// - a timeout of 0 with the caller not owning the mutex, where the variable starts at 0
// - a wait with callbacks and a callback pending at the call, with the caller not owning the
//   mutex, which also prints whether the callback ran
// - a waiter that a mutex delete wakes while it waits on the condition variable, then while it
//   waits to lock the mutex again after a signal, heavy only
// Lightweight mutexes are never deleted here. The waiters are pinned to core 0 below the main
// thread, so a woken waiter stays ready until the main thread blocks. The two waiter cases each
// sleep 200 ms before the mutex delete or the signal, so the time left is far from 1.5 s and 0.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define PRESET 1500000
#define PASSES_US 200000
#define UNKNOWN_UID 0x12345

static SceUID mutex, cond;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
// A work area that never held a condition variable, so its UID is 0
static SceKernelLwCondWork never_created;
static volatile int waiter_result, callback_ran;
static volatile SceUInt waiter_left;

// The wait type of thread, without its callback bit, or 0 if it isn't waiting
static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    if (sceKernelGetThreadInfo(thread, &info) < 0 || !(info.status & SCE_THREAD_WAITING))
        return 0;
    return info.waitType & ~WAIT_TYPE_CB;
}

// Prints the result of a wait, and whether the time left it left in its variable is the preset, 0,
// or between 0 and the preset
static void report(const char *name, int result, SceUInt preset, SceUInt left) {
    PRINTF(result, "%s: the wait", name);
    PRINTF(left == preset, "%s: the time left is unchanged", name);
    PRINTF(left == 0, "%s: the time left is 0", name);
    PRINTF(left != 0 && left < preset, "%s: the time left is between 0 and the preset", name);
}

static void create_heavy(void) {
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
}

static void create_lw(void) {
    sceKernelCreateLwMutex(&lw_mutex, "mutex", 0, 0, NULL);
    sceKernelCreateLwCond(&lw_cond, "cond", 0, &lw_mutex, NULL);
}

// The caller doesn't own the mutex, which nobody holds
static void heavy_not_owner(void) {
    create_heavy();
    SceUInt timeout = PRESET;
    const int result = sceKernelWaitCond(cond, &timeout);
    report("heavy, not the owner", result, PRESET, timeout);
    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(mutex);
}

static void lw_not_owner(void) {
    create_lw();
    SceUInt timeout = PRESET;
    const int result = sceKernelWaitLwCond(&lw_cond, &timeout);
    report("lightweight, not the owner", result, PRESET, timeout);
    sceKernelDeleteLwCond(&lw_cond);
}

static void heavy_unknown(void) {
    SceUInt timeout = PRESET;
    const int result = sceKernelWaitCond(UNKNOWN_UID, &timeout);
    report("heavy, unknown condition variable", result, PRESET, timeout);
}

static void lw_unknown(void) {
    SceUInt timeout = PRESET;
    const int result = sceKernelWaitLwCond(&never_created, &timeout);
    report("lightweight, unknown condition variable", result, PRESET, timeout);
}

static void heavy_deleted(void) {
    create_heavy();
    sceKernelDeleteCond(cond);
    SceUInt timeout = PRESET;
    const int result = sceKernelWaitCond(cond, &timeout);
    report("heavy, deleted condition variable", result, PRESET, timeout);
    sceKernelDeleteMutex(mutex);
}

static void lw_deleted(void) {
    create_lw();
    sceKernelDeleteLwCond(&lw_cond);
    SceUInt timeout = PRESET;
    const int result = sceKernelWaitLwCond(&lw_cond, &timeout);
    report("lightweight, deleted condition variable", result, PRESET, timeout);
}

// The mutex is deleted first, so the condition variable has none
static void heavy_mutex_deleted(void) {
    create_heavy();
    sceKernelDeleteMutex(mutex);
    SceUInt timeout = PRESET;
    const int result = sceKernelWaitCond(cond, &timeout);
    report("heavy, condition variable whose mutex is deleted", result, PRESET, timeout);
    sceKernelDeleteCond(cond);
}

static void heavy_timeout_zero(void) {
    create_heavy();
    SceUInt timeout = 0;
    const int result = sceKernelWaitCond(cond, &timeout);
    report("heavy, timeout 0 and not the owner", result, 0, timeout);
    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(mutex);
}

static void lw_timeout_zero(void) {
    create_lw();
    SceUInt timeout = 0;
    const int result = sceKernelWaitLwCond(&lw_cond, &timeout);
    report("lightweight, timeout 0 and not the owner", result, 0, timeout);
    sceKernelDeleteLwCond(&lw_cond);
}

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_ran = 1;
    return 0;
}

// Notifies a callback of this thread, so it is pending when the wait with callbacks starts
static void heavy_pending_callback(void) {
    create_heavy();
    const SceUID callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    sceKernelNotifyCallback(callback, 1);
    callback_ran = 0;
    SceUInt timeout = PRESET;
    const int result = sceKernelWaitCondCB(cond, &timeout);
    report("heavy, callback pending and not the owner", result, PRESET, timeout);
    PRINT("heavy, callback pending and not the owner: the callback ran", callback_ran);
    sceKernelDeleteCallback(callback);
    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(mutex);
}

static void lw_pending_callback(void) {
    create_lw();
    const SceUID callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    sceKernelNotifyCallback(callback, 1);
    callback_ran = 0;
    SceUInt timeout = PRESET;
    const int result = sceKernelWaitLwCondCB(&lw_cond, &timeout);
    report("lightweight, callback pending and not the owner", result, PRESET, timeout);
    PRINT("lightweight, callback pending and not the owner: the callback ran", callback_ran);
    sceKernelDeleteCallback(callback);
    sceKernelDeleteLwCond(&lw_cond);
}

// Locks the mutex, then waits on the condition variable with the preset timeout, and records the
// result and the time left
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt lock_timeout = TEST_WAIT_US;
    sceKernelLockMutex(mutex, 1, &lock_timeout);
    SceUInt timeout = PRESET;
    waiter_result = sceKernelWaitCond(cond, &timeout);
    waiter_left = timeout;
    return 0;
}

static SceUID start_waiter(void) {
    waiter_result = 0xDEAD;
    waiter_left = 0xDEAD;
    const SceUID thread = sceKernelCreateThread("waiter", waiter, WAITER_PRIORITY, 0x4000, 0,
        SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

// The mutex is deleted while the waiter waits on the condition variable
static void heavy_delete_mutex_in_cond_wait(void) {
    create_heavy();
    const SceUID thread = start_waiter();
    TEST_WAIT_FOR(wait_type_of(thread) == WAIT_TYPE_COND);
    sceKernelDelayThread(PASSES_US);
    const int deleted = sceKernelDeleteMutex(mutex);
    test_join_thread(thread);
    PRINT("heavy, mutex deleted while the waiter waits on the condition: the delete", deleted);
    report("heavy, mutex deleted while the waiter waits on the condition", waiter_result, PRESET,
        waiter_left);
    sceKernelDeleteCond(cond);
}

// The main thread holds the mutex and signals, so the waiter waits to lock it again, then the
// mutex is deleted
static void heavy_delete_mutex_in_relock(void) {
    create_heavy();
    const SceUID thread = start_waiter();
    TEST_WAIT_FOR(wait_type_of(thread) == WAIT_TYPE_COND);
    SceUInt lock_timeout = TEST_WAIT_US;
    sceKernelLockMutex(mutex, 1, &lock_timeout);
    sceKernelDelayThread(PASSES_US);
    sceKernelSignalCond(cond);
    TEST_WAIT_FOR(wait_type_of(thread) == WAIT_TYPE_COND_MUTEX);
    const int deleted = sceKernelDeleteMutex(mutex);
    test_join_thread(thread);
    PRINT("heavy, mutex deleted while the waiter relocks: the delete", deleted);
    report("heavy, mutex deleted while the waiter relocks", waiter_result, PRESET, waiter_left);
    sceKernelDeleteCond(cond);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    heavy_not_owner();
    lw_not_owner();
    heavy_unknown();
    lw_unknown();
    heavy_deleted();
    lw_deleted();
    heavy_mutex_deleted();
    heavy_timeout_zero();
    lw_timeout_zero();
    heavy_pending_callback();
    lw_pending_callback();
    heavy_delete_mutex_in_cond_wait();
    heavy_delete_mutex_in_relock();

    test_exit();
    return 0;
}
