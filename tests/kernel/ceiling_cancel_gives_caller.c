// What cancelling a ceiling mutex does to the priority of the thread that gets it, the caller. The
// main thread is on core 1 at 0xA0. Owner O, at 0xB0 on core 0, locks the mutex and waits on a
// semaphore while the main thread cancels it. The mutexes have the ceiling 0x80. The cases:
// (a) A mutex created with the count 1 and unlocked, then locked by O and cancelled with -1. The
//     main thread prints O's priority, its own priority, and the count and owner after the cancel,
//     then unlocks once and reads its priority.
// (b) A recursive mutex held by O and cancelled to 2. The main thread reads its priority and the
//     count, then unlocks once and reads its priority, and again.
// (c) A mutex the main thread locks itself and cancels to 1, with its priority and the count and
//     owner after, then the unlock and its priority. The same with a cancel to 0.
// (d) The main thread at 0x60 cancels to 1 a mutex held by O and reads its priority. It then sets
//     its own priority to 0xA0 and reads it, then unlocks and reads it again.
// (e) O holds a mutex and W, at 0x90 on core 0, waits for it. The main thread cancels it to 1. It
//     prints the number of waiters the cancel writes, W's lock result, W's priority after the lock,
//     O's priority and its own.
// Nothing is printed while a case runs, the main thread prints once every thread has ended.
//
// On a PS Vita TV on firmware 3.65 a cancel that leaves the count at 1 or more gives the
// mutex to the caller like a lock, so the caller is raised to 0x80. In (a) O stays at 0xB0 and the
// main thread is at 0x80 with count 1, owning it. The unlock puts it back to 0xA0. In (b) the main
// thread is at 0x80 with count 2, still at 0x80 after the first unlock with count 1, and at 0xA0
// after the second. In (c) the cancel to 1 leaves the main thread at 0x80 with count 1 as the owner,
// the unlock puts it at 0xA0, and the cancel to 0 leaves it at 0xA0 with count 0. In (d) the main
// thread stays at 0x60, since it is already above the ceiling. Setting 0xA0 is only stored, so it
// still reads 0x60, and the unlock applies it. In (e) the cancel writes 1 waiter and W's lock fails
// with 0x80028007. W stays at 0x90, O at 0xB0 and the main thread is at 0x80.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define OWNER_PRIORITY 0xB0
#define WAITER_PRIORITY 0x90
#define CEILING 0x80

static SceUID mutex, sema;
static int lock_result, wait_result, waiter_lock, waiter_priority;

static int current_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.currentPriority;
}

static MutexInfo info_of(SceUID m) {
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(m, (SceKernelMutexInfo *)&info);
    return info;
}

// Locks the mutex and waits on the semaphore
static int owner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    lock_result = sceKernelLockMutex(mutex, 1, NULL);
    SceUInt32 timeout = 4 * TEST_WAIT_US;
    wait_result = sceKernelWaitSema(sema, 1, &timeout);
    return 0;
}

// Locks the mutex with a timeout and reads its own priority
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 4 * TEST_WAIT_US;
    waiter_lock = sceKernelLockMutex(mutex, 1, &timeout);
    waiter_priority = sceKernelGetThreadCurrentPriority();
    return 0;
}

static SceUID make_mutex(int attr, int init_count) {
    SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = CEILING };
    return sceKernelCreateMutex("ceiling", attr, init_count, &opt);
}

static SceUID start_owner(void) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    lock_result = wait_result = -1;
    const SceUID owner = sceKernelCreateThread("owner", owner_entry, OWNER_PRIORITY, 0x4000, 0,
                                               SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(owner, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(owner, WAIT_TYPE_SEMA, sema));
    return owner;
}

static void end_owner(SceUID owner) {
    sceKernelSignalSema(sema, 1);
    test_join_thread(owner);
    sceKernelDeleteSema(sema);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    const SceUID main_thread = sceKernelGetThreadId();

    // (a) created with 1 and unlocked, held by O, cancelled with -1
    mutex = make_mutex(SCE_KERNEL_MUTEX_ATTR_CEILING, 1);
    const int a_unlock0 = sceKernelUnlockMutex(mutex, 1);
    SceUID owner = start_owner();
    const int a_lock = lock_result;
    const int a_owner_before = current_of(owner);
    int a_waiters = -1;
    const int a_cancel = sceKernelCancelMutex(mutex, -1, &a_waiters);
    const int a_owner = current_of(owner);
    const int a_main = sceKernelGetThreadCurrentPriority();
    const MutexInfo a_info = info_of(mutex);
    const int a_unlock = sceKernelUnlockMutex(mutex, 1);
    const int a_main_after = sceKernelGetThreadCurrentPriority();
    end_owner(owner);
    const int a_lock_end = lock_result, a_wait = wait_result;
    sceKernelDeleteMutex(mutex);

    // (b) recursive, held by O, cancelled to 2
    mutex = make_mutex(SCE_KERNEL_MUTEX_ATTR_CEILING | SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0);
    owner = start_owner();
    int b_waiters = -1;
    const int b_cancel = sceKernelCancelMutex(mutex, 2, &b_waiters);
    const int b_main = sceKernelGetThreadCurrentPriority();
    const MutexInfo b_info = info_of(mutex);
    const int b_unlock1 = sceKernelUnlockMutex(mutex, 1);
    const int b_main1 = sceKernelGetThreadCurrentPriority();
    const MutexInfo b_info1 = info_of(mutex);
    const int b_unlock2 = sceKernelUnlockMutex(mutex, 1);
    const int b_main2 = sceKernelGetThreadCurrentPriority();
    end_owner(owner);
    sceKernelDeleteMutex(mutex);

    // (c) locked by the main thread, cancelled to 1, and again to 0
    mutex = make_mutex(SCE_KERNEL_MUTEX_ATTR_CEILING, 0);
    const int c_lock = sceKernelLockMutex(mutex, 1, NULL);
    const int c_main_locked = sceKernelGetThreadCurrentPriority();
    const int c_cancel1 = sceKernelCancelMutex(mutex, 1, NULL);
    const int c_main1 = sceKernelGetThreadCurrentPriority();
    const MutexInfo c_info1 = info_of(mutex);
    const int c_unlock = sceKernelUnlockMutex(mutex, 1);
    const int c_main_unlocked = sceKernelGetThreadCurrentPriority();
    sceKernelDeleteMutex(mutex);
    mutex = make_mutex(SCE_KERNEL_MUTEX_ATTR_CEILING, 0);
    sceKernelLockMutex(mutex, 1, NULL);
    const int c_cancel0 = sceKernelCancelMutex(mutex, 0, NULL);
    const int c_main0 = sceKernelGetThreadCurrentPriority();
    const MutexInfo c_info0 = info_of(mutex);
    sceKernelDeleteMutex(mutex);

    // (d) the main thread at 0x60 cancels a mutex held by O to 1
    mutex = make_mutex(SCE_KERNEL_MUTEX_ATTR_CEILING, 0);
    owner = start_owner();
    sceKernelChangeThreadPriority(0, 0x60);
    const int d_cancel = sceKernelCancelMutex(mutex, 1, NULL);
    const int d_main = sceKernelGetThreadCurrentPriority();
    const int d_set = sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    const int d_main_set = sceKernelGetThreadCurrentPriority();
    const int d_unlock = sceKernelUnlockMutex(mutex, 1);
    const int d_main_unlock = sceKernelGetThreadCurrentPriority();
    end_owner(owner);
    sceKernelDeleteMutex(mutex);

    // (e) O holds it, W waits for it, and the main thread cancels it to 1
    mutex = make_mutex(SCE_KERNEL_MUTEX_ATTR_CEILING, 0);
    owner = start_owner();
    waiter_lock = waiter_priority = -1;
    const SceUID waiter = sceKernelCreateThread("waiter", waiter_entry, WAITER_PRIORITY, 0x4000, 0,
                                                SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(waiter, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(waiter, WAIT_TYPE_MUTEX, mutex));
    int e_waiters = -1;
    const int e_cancel = sceKernelCancelMutex(mutex, 1, &e_waiters);
    test_join_thread(waiter);
    const int e_owner = current_of(owner);
    const int e_main = sceKernelGetThreadCurrentPriority();
    const int e_unlock = sceKernelUnlockMutex(mutex, 1);
    end_owner(owner);
    sceKernelDeleteMutex(mutex);
    (void)main_thread;

    PRINTF(a_unlock0, "a: the unlock after the create");
    PRINTF(a_lock, "a: O's lock");
    PRINTF(a_owner_before, "a: O's priority before the cancel");
    PRINTF(a_cancel, "a: the cancel with -1");
    PRINTF(a_waiters, "a: the number of waiters it writes");
    PRINTF(a_owner, "a: O's priority after");
    PRINTF(a_main, "a: the main thread's priority after");
    PRINTF(a_info.currentCount, "a: the count after");
    PRINTF(a_info.currentOwnerId == main_thread, "a: the main thread owns it");
    PRINTF(a_unlock, "a: the unlock");
    PRINTF(a_main_after, "a: the main thread's priority after the unlock");
    PRINTF(a_lock_end, "a: O's lock at the end");
    PRINTF(a_wait, "a: O's wait");

    PRINTF(b_cancel, "b: the cancel to 2");
    PRINTF(b_waiters, "b: the number of waiters it writes");
    PRINTF(b_main, "b: the main thread's priority after");
    PRINTF(b_info.currentCount, "b: the count after");
    PRINTF(b_unlock1, "b: the first unlock");
    PRINTF(b_main1, "b: the main thread's priority after it");
    PRINTF(b_info1.currentCount, "b: the count after it");
    PRINTF(b_unlock2, "b: the second unlock");
    PRINTF(b_main2, "b: the main thread's priority after it");

    PRINTF(c_lock, "c: the main thread's lock");
    PRINTF(c_main_locked, "c: its priority after the lock");
    PRINTF(c_cancel1, "c: the cancel to 1");
    PRINTF(c_main1, "c: the main thread's priority after it");
    PRINTF(c_info1.currentCount, "c: the count after it");
    PRINTF(c_info1.currentOwnerId == main_thread, "c: the main thread owns it");
    PRINTF(c_unlock, "c: the unlock");
    PRINTF(c_main_unlocked, "c: the main thread's priority after it");
    PRINTF(c_cancel0, "c: the cancel to 0");
    PRINTF(c_main0, "c: the main thread's priority after it");
    PRINTF(c_info0.currentCount, "c: the count after it");

    PRINTF(d_cancel, "d: the cancel to 1 at 0x60");
    PRINTF(d_main, "d: the main thread's priority after it");
    PRINTF(d_set, "d: the change to 0xA0");
    PRINTF(d_main_set, "d: the main thread's priority after it");
    PRINTF(d_unlock, "d: the unlock");
    PRINTF(d_main_unlock, "d: the main thread's priority after it");

    PRINTF(e_cancel, "e: the cancel to 1");
    PRINTF(e_waiters, "e: the number of waiters it writes");
    PRINTF(waiter_lock, "e: W's lock");
    PRINTF(waiter_priority, "e: W's priority after its lock");
    PRINTF(e_owner, "e: O's priority");
    PRINTF(e_main, "e: the main thread's priority");
    PRINTF(e_unlock, "e: the unlock");

    test_exit();
    return 0;
}
