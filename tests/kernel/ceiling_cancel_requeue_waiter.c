// Whether a ceiling that another thread's cancel drops moves its owner back in the priority queue it
// waits in, so a waiter behind it takes what it can. Owner O, at 0xB0 on core 0, locks a mutex M
// with the ceiling 0x80, which raises it to 0x80, and waits for 2 on a semaphore S with
// priority order that has a count of 1. W, at 0x90 on core 0, then waits for 1 on S behind O. The
// main thread, at 0xA0 on core 1, cancels M to 0, which takes O off its ceiling, and reads O's
// priority and S's count and waiters right after. Then it signals S with 2 and waits for both
// threads to end. It prints once both have ended.
//
// On a PS Vita TV on firmware 3.65 the cancel puts O back to 0xB0 and moves it behind W, so W takes
// the count at once: right after the cancel S has a count of 0 and one waiter, O. The signal of 2
// then gives O its 2.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define OWNER_PRIORITY 0xB0
#define WAITER_PRIORITY 0x90
#define CEILING 0x80

static SceUID mutex, sema;
static int lock_result = -1, owner_wait = -1, waiter_wait = -1;

static int owner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    lock_result = sceKernelLockMutex(mutex, 1, NULL);
    SceUInt32 timeout = 4 * TEST_WAIT_US;
    owner_wait = sceKernelWaitSema(sema, 2, &timeout);
    return 0;
}

static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 4 * TEST_WAIT_US;
    waiter_wait = sceKernelWaitSema(sema, 1, &timeout);
    return 0;
}

static int current_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.currentPriority;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = CEILING };
    mutex = sceKernelCreateMutex("ceiling", SCE_KERNEL_MUTEX_ATTR_CEILING, 0, &opt);
    sema = sceKernelCreateSema("sema", SCE_KERNEL_ATTR_THREAD_PRIO, 1, 2, NULL);
    const SceUID owner = sceKernelCreateThread("owner", owner_entry, OWNER_PRIORITY, 0x4000, 0,
                                               SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID waiter = sceKernelCreateThread("waiter", waiter_entry, WAITER_PRIORITY, 0x4000, 0,
                                                SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(owner, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(owner, WAIT_TYPE_SEMA, sema));
    sceKernelStartThread(waiter, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(waiter, WAIT_TYPE_SEMA, sema));
    const int owner_before = current_of(owner);

    const int cancel = sceKernelCancelMutex(mutex, 0, NULL);
    const int owner_after = current_of(owner);
    SceKernelSemaInfo info = { .size = sizeof(info) };
    sceKernelGetSemaInfo(sema, &info);
    const int count_after = info.currentCount;
    const int waiters_after = info.numWaitThreads;
    const int waiter_waits_after = test_in_wait(waiter, WAIT_TYPE_SEMA, sema);

    sceKernelSignalSema(sema, 2);
    test_join_thread(owner);
    test_join_thread(waiter);

    PRINT("O's priority with the mutex", owner_before);
    PRINT("the cancel", cancel);
    PRINT("O's priority after the cancel", owner_after);
    PRINT("S's count right after the cancel", count_after);
    PRINT("S's waiters right after the cancel", waiters_after);
    PRINT("W still waits right after the cancel", waiter_waits_after);
    PRINT("O's lock", lock_result);
    PRINT("O's wait", owner_wait);
    PRINT("W's wait", waiter_wait);

    sceKernelDeleteThread(owner);
    sceKernelDeleteThread(waiter);
    sceKernelDeleteSema(sema);
    sceKernelDeleteMutex(mutex);
    test_exit();
    return 0;
}
