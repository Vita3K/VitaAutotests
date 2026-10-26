// What cancelling a ceiling mutex does to the priority of the thread that holds it. Owner O, at
// 0xB0 on core 0, locks a mutex M1 with the ceiling 0x80 and waits on a semaphore. The main thread,
// on core 1 at 0xA0, reads O's priority, cancels M1 to 0 and reads O's priority again, with the
// count and owner of M1 after the cancel. Then O locks a second mutex M2 with the ceiling 0x80 and
// waits on another semaphore. The main thread reads O's priority, cancels M2 to 1 and reads O's
// priority again, with the count and owner of M2. It then reads its own priority, unlocks M2 once
// and reads its priority again. The cases:
// - M1 cancelled to 0 while O holds it, which leaves it free
// - M2 cancelled to 1 while O holds it, which gives it to the main thread
// The number of waiters each cancel writes, and the results of O's locks and waits, are printed
// too. The main thread prints once O has ended, so nothing is printed while the cancels run.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define OWNER_PRIORITY 0xB0
#define CEILING 0x80

// The two mutexes, and a semaphore for each that O waits on while it holds the mutex
static SceUID mutexes[2], semas[2];
static int lock_results[2], wait_results[2];

// Locks M1, waits on its semaphore, then locks M2 and waits on its semaphore
static int owner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    lock_results[0] = sceKernelLockMutex(mutexes[0], 1, NULL);
    SceUInt32 timeout = 2 * TEST_WAIT_US;
    wait_results[0] = sceKernelWaitSema(semas[0], 1, &timeout);
    lock_results[1] = sceKernelLockMutex(mutexes[1], 1, NULL);
    timeout = 2 * TEST_WAIT_US;
    wait_results[1] = sceKernelWaitSema(semas[1], 1, &timeout);
    return 0;
}

static int current_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.currentPriority;
}

static MutexInfo info_of(SceUID mutex) {
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = CEILING };
    for (int i = 0; i < 2; i++) {
        mutexes[i] = sceKernelCreateMutex("ceiling", SCE_KERNEL_MUTEX_ATTR_CEILING, 0, &opt);
        semas[i] = sceKernelCreateSema("sema", 0, 0, 1, NULL);
        lock_results[i] = wait_results[i] = -1;
    }
    const SceUID owner = sceKernelCreateThread("owner", owner_entry, OWNER_PRIORITY, 0x4000, 0,
                                               SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(owner, 0, NULL);

    // M1: O holds it and waits, and the main thread cancels it to 0
    TEST_WAIT_FOR(test_in_wait(owner, WAIT_TYPE_SEMA, semas[0]));
    const int before1 = current_of(owner);
    int waiters1 = -1;
    const int cancel1 = sceKernelCancelMutex(mutexes[0], 0, &waiters1);
    const int after1 = current_of(owner);
    const MutexInfo info1 = info_of(mutexes[0]);
    sceKernelSignalSema(semas[0], 1);

    // M2: O holds it and waits, and the main thread cancels it to 1
    TEST_WAIT_FOR(test_in_wait(owner, WAIT_TYPE_SEMA, semas[1]));
    const int before2 = current_of(owner);
    int waiters2 = -1;
    const int cancel2 = sceKernelCancelMutex(mutexes[1], 1, &waiters2);
    const int after2 = current_of(owner);
    const MutexInfo info2 = info_of(mutexes[1]);
    const int main_after_cancel = sceKernelGetThreadCurrentPriority();
    const int unlock2 = sceKernelUnlockMutex(mutexes[1], 1);
    const int main_after_unlock = sceKernelGetThreadCurrentPriority();
    sceKernelSignalSema(semas[1], 1);

    test_join_thread(owner);
    PRINTF(before1, "M1 cancel to 0: O's priority before it");
    PRINTF(cancel1, "M1 cancel to 0: the cancel");
    PRINTF(waiters1, "M1 cancel to 0: the number of waiters it writes");
    PRINTF(after1, "M1 cancel to 0: O's priority after it");
    PRINTF(info1.currentCount, "M1 cancel to 0: the count after");
    PRINTF(info1.currentOwnerId == owner, "M1 cancel to 0: O still owns it");
    PRINTF(lock_results[0], "M1 cancel to 0: O's lock");
    PRINTF(wait_results[0], "M1 cancel to 0: O's wait");

    PRINTF(before2, "M2 cancel to 1: O's priority before it");
    PRINTF(cancel2, "M2 cancel to 1: the cancel");
    PRINTF(waiters2, "M2 cancel to 1: the number of waiters it writes");
    PRINTF(after2, "M2 cancel to 1: O's priority after it");
    PRINTF(info2.currentCount, "M2 cancel to 1: the count after");
    PRINTF(info2.currentOwnerId == sceKernelGetThreadId(),
           "M2 cancel to 1: the main thread owns it");
    PRINTF(main_after_cancel, "M2 cancel to 1: the main thread's priority after the cancel");
    PRINTF(unlock2, "M2 cancel to 1: the main thread's unlock of 1");
    PRINTF(main_after_unlock, "M2 cancel to 1: the main thread's priority after the unlock");
    PRINTF(lock_results[1], "M2 cancel to 1: O's lock");
    PRINTF(wait_results[1], "M2 cancel to 1: O's wait");

    for (int i = 0; i < 2; i++) {
        sceKernelDeleteSema(semas[i]);
        sceKernelDeleteMutex(mutexes[i]);
    }

    test_exit();
    return 0;
}
