// Whether deleting a mutex switches to a higher priority thread it woke before the delete returns.
// Everything runs on core 0, the main thread is below the waiter W. W sets a flag as soon as its
// wait returns. The main thread reads that flag right after the delete returns, with no call in
// between, and records it. Then it joins W and prints the delete's result, W's result and the flag.
// Cases:
// - Condition waiter: W locks the mutex and waits on a condition bound to it, so the mutex is free.
// - Condition waiter, owned mutex: the same, but the main thread locks the mutex before deleting it.
// - Mutex waiter: the main thread owns the mutex and W waits to lock it.
// - Lightweight condition waiter: the same as the first case with a lightweight mutex and condition.
//
// On a PS Vita TV on firmware 3.65 the mutex waiter has run when the delete returns. The
// condition waiter has not run in the heavy cases, since the delete drops the dispatch request of the
// wakes of the conditions, and has run in the lightweight case, which keeps it. W's result is
// SCE_KERNEL_ERROR_WAIT_DELETE_MUTEX for the heavy condition, SCE_KERNEL_ERROR_WAIT_DELETE for the
// mutex waiter and SCE_KERNEL_ERROR_WAIT_DELETE_LW_MUTEX for the lightweight condition.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define WAITER_PRIORITY 0x80

typedef enum Kind { COND, COND_OWNED, MUTEX, LW_COND } Kind;

static Kind kind;
static SceUID mutex, cond;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static volatile int w_result, w_ran;

static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    switch (kind) {
    case COND:
    case COND_OWNED:
        sceKernelLockMutex(mutex, 1, NULL);
        w_result = sceKernelWaitCond(cond, &timeout);
        break;
    case MUTEX: w_result = sceKernelLockMutex(mutex, 1, &timeout); break;
    case LW_COND:
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        w_result = sceKernelWaitLwCond(&lw_cond, &timeout);
        break;
    }
    w_ran = 1;
    return 0;
}

static void run_case(const char *name, Kind k) {
    kind = k;
    w_result = -1;
    w_ran = 0;
    SceUID wait_id;
    if (k == LW_COND) {
        sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "lw cond", 0, &lw_mutex, NULL);
        LwCondInfo info = { .size = sizeof(info) };
        wait_id = sceKernelGetLwCondInfo(&lw_cond, &info) == 0 ? info.uid : -1;
    } else {
        mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
        cond = sceKernelCreateCond("cond", 0, mutex, NULL);
        wait_id = k == MUTEX ? mutex : cond;
        if (k == MUTEX)
            sceKernelLockMutex(mutex, 1, NULL);
    }
    const SceUInt32 wait_type = k == LW_COND ? WAIT_TYPE_LW_COND : k == MUTEX ? WAIT_TYPE_MUTEX : WAIT_TYPE_COND;
    const SceUID w = sceKernelCreateThread("w", w_entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(w, wait_type, wait_id));
    if (k == COND_OWNED)
        sceKernelLockMutex(mutex, 1, NULL);
    const int deleted = k == LW_COND ? sceKernelDeleteLwMutex(&lw_mutex) : sceKernelDeleteMutex(mutex);
    const int ran_at_delete = w_ran;
    test_join_thread(w);
    PRINTF(deleted, "%s: delete the mutex", name);
    PRINTF(ran_at_delete, "%s: W already ran when the delete returned", name);
    PRINTF(w_result, "%s: W's wait", name);
    PRINTF(w_ran, "%s: W ran after the join", name);
    if (k == LW_COND) {
        sceKernelDeleteLwCond(&lw_cond);
    } else {
        sceKernelDeleteCond(cond);
    }
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("condition waiter", COND);
    run_case("condition waiter, mutex owned by the deleter", COND_OWNED);
    run_case("mutex waiter", MUTEX);
    run_case("lightweight condition waiter", LW_COND);

    test_exit();
    return 0;
}
