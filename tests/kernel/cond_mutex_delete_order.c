// Which thread runs first after a mutex delete when one thread waits on the condition variable of
// the mutex and another waits to lock the mutex itself. Both are at 0x80 and pinned to core 0, the
// main thread is at 0x60 above them, so they run only when it blocks, and the order they run in is
// the order their waits ended in. C locks the mutex and waits on the condition variable, then the
// main thread locks the mutex and L starts and waits to lock it. The main thread then deletes the
// mutex and waits for both. Each case prints the delete's result, the run order of C and L as two
// hex digits (1 for C, 2 for L, the first to run in the high digit), and each wait result. The
// cases are the heavy and the lightweight mutex, each FIFO and with SCE_KERNEL_ATTR_THREAD_PRIO on
// the mutex and the condition variable.
//
// On a PS Vita TV on firmware 3.65 the condition waiter C runs before the mutex waiter L in all four
// cases. C's wait ends with 0x800281A2 for the heavy mutex and 0x800281C2 for the lightweight one, and
// L's lock with 0x80028006.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0x80
// Longer than both polls for the waits together, so neither waiter leaves before the delete
#define WAITER_TIMEOUT_US 20000000

static SceUID mutex, cond;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static volatile int lightweight, run_count, run_order[2], wait_result[2];

static void record(int digit, int result) {
    wait_result[digit - 1] = result;
    const int n = __atomic_fetch_add(&run_count, 1, __ATOMIC_SEQ_CST);
    if (n < 2)
        run_order[n] = digit;
}

// Locks the mutex, waits on the condition variable and records the wait's result
static int cond_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = WAITER_TIMEOUT_US;
    int result;
    if (lightweight) {
        sceKernelLockLwMutex(&lw_mutex, 1, &timeout);
        timeout = WAITER_TIMEOUT_US;
        result = sceKernelWaitLwCond(&lw_cond, &timeout);
    } else {
        sceKernelLockMutex(mutex, 1, &timeout);
        timeout = WAITER_TIMEOUT_US;
        result = sceKernelWaitCond(cond, &timeout);
    }
    record(1, result);
    return 0;
}

// Waits to lock the mutex and records the lock's result
static int locker(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = WAITER_TIMEOUT_US;
    const int result = lightweight ? sceKernelLockLwMutex(&lw_mutex, 1, &timeout)
                                   : sceKernelLockMutex(mutex, 1, &timeout);
    record(2, result);
    return 0;
}

static int in_wait_type(SceUID thread, SceUInt32 type) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType == type;
}

static SceUID start(SceKernelThreadEntry entry) {
    const SceUID thread = sceKernelCreateThread("waiter", entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

static void run_case(int lw, SceUInt32 attr, const char *name) {
    lightweight = lw;
    run_count = 0;
    run_order[0] = run_order[1] = 0xF;
    wait_result[0] = wait_result[1] = 0xDEAD;
    if (lw) {
        sceKernelCreateLwMutex(&lw_mutex, "mutex", attr, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "cond", attr, &lw_mutex, NULL);
    } else {
        mutex = sceKernelCreateMutex("mutex", attr, 0, NULL);
        cond = sceKernelCreateCond("cond", attr, mutex, NULL);
    }
    const SceUID c = start(cond_waiter);
    TEST_WAIT_FOR(in_wait_type(c, lw ? WAIT_TYPE_LW_COND : WAIT_TYPE_COND));
    SceUInt32 timeout = TEST_WAIT_US;
    if (lw)
        sceKernelLockLwMutex(&lw_mutex, 1, &timeout);
    else
        sceKernelLockMutex(mutex, 1, &timeout);
    const SceUID l = start(locker);
    TEST_WAIT_FOR(in_wait_type(l, lw ? WAIT_TYPE_LW_MUTEX : WAIT_TYPE_MUTEX));
    const int both_wait = in_wait_type(c, lw ? WAIT_TYPE_LW_COND : WAIT_TYPE_COND) && in_wait_type(l, lw ? WAIT_TYPE_LW_MUTEX : WAIT_TYPE_MUTEX);

    const int deleted = lw ? sceKernelDeleteLwMutex(&lw_mutex) : sceKernelDeleteMutex(mutex);
    test_join_thread(c);
    test_join_thread(l);
    PRINTF(both_wait, "%s: C and L wait before the delete", name);
    PRINTF(deleted, "%s: the delete", name);
    PRINTF((run_order[0] << 4) | run_order[1], "%s: run order", name);
    PRINTF(wait_result[0], "%s: C's wait result", name);
    PRINTF(wait_result[1], "%s: L's lock result", name);
    if (lw)
        sceKernelDeleteLwCond(&lw_cond);
    else
        sceKernelDeleteCond(cond);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case(0, 0, "heavy, FIFO");
    run_case(0, SCE_KERNEL_ATTR_THREAD_PRIO, "heavy, priority order");
    run_case(1, 0, "lightweight, FIFO");
    run_case(1, SCE_KERNEL_ATTR_THREAD_PRIO, "lightweight, priority order");

    test_exit();
    return 0;
}
