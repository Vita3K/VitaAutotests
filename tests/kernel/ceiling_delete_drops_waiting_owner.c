// What deleting a ceiling mutex does to its owner's priority, and to a thread that waits on it.
// The main thread runs on core 1 at 0xA0, every other thread on core 0.
// (a) Owner O, at 0xB0, locks a mutex M with the ceiling 0x80 and waits for 2 on a semaphore S
// with priority order that has a count of 1. W, at 0x90, then waits for 1 on S behind O. The main
// thread deletes M and reads O's priority and S's count and waiters right after, and whether W
// still waits. Then it signals S with 2 and prints the wait results of O and W.
// (b) O locks a mutex B with the ceiling 0x80 and then a mutex A with the ceiling 0x70, and waits
// on a semaphore. The main thread deletes A and reads O's priority, then deletes B and reads it
// again.
// (c) Y, at 0xB8, locks a mutex M2 with the ceiling 0x80 and waits on a condition variable bound
// to M2, which releases it. O then locks M2 and waits on a semaphore, and X, at 0xB8, waits to
// lock M2. The main thread deletes M2 and reads O's priority. Y and X record their wait results
// and their own priority once they wake.
//
// On a PS Vita TV on firmware 3.65 the delete takes O off the ceiling like a cancel
// does, so in (a) O goes back to 0xB0 behind W in S's queue and W takes the count at once: right
// after the delete S has a count of 0 and one waiter, O, and W no longer waits. The signal of 2
// then gives O its 2. In (b) the delete of A leaves O at the ceiling of B, 0x80, and the delete
// of B puts it back to 0xB0. In (c) the delete wakes Y with 0x800281A2 and X with 0x80028006,
// both stay at 0xB8, and O goes back to 0xB0.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define OWNER_PRIORITY 0xB0
#define WAITER_PRIORITY 0x90
#define OTHER_PRIORITY 0xB8
#define CEILING 0x80
#define LOW_CEILING 0x70

static SceUID mutex, mutex_b, cond, sema;
static volatile int owner_wait = -1, waiter_wait = -1;
static volatile int y_wait = -1, y_priority = -1, x_lock = -1, x_priority = -1;

static int current_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.currentPriority;
}

static SceUID make_ceiling_mutex(const char *name, int ceiling) {
    SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = ceiling };
    return sceKernelCreateMutex(name, SCE_KERNEL_MUTEX_ATTR_CEILING, 0, &opt);
}

static SceUID make_thread(const char *name, SceKernelThreadEntry entry, int priority) {
    return sceKernelCreateThread(name, entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0,
                                 NULL);
}

// Locks M and waits for 2 on S
static int owner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 1, NULL);
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

// Locks B, then M, and waits on S
static int two_owner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex_b, 1, NULL);
    sceKernelLockMutex(mutex, 1, NULL);
    SceUInt32 timeout = 4 * TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    return 0;
}

// Locks M2 and waits on the condition variable
static int y_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    SceUInt32 timeout = 4 * TEST_WAIT_US;
    y_wait = sceKernelWaitCond(cond, &timeout);
    y_priority = sceKernelGetThreadCurrentPriority();
    return 0;
}

static int x_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 4 * TEST_WAIT_US;
    x_lock = sceKernelLockMutex(mutex, 1, &timeout);
    x_priority = sceKernelGetThreadCurrentPriority();
    return 0;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    // Case (a)
    mutex = make_ceiling_mutex("ceiling", CEILING);
    sema = sceKernelCreateSema("sema", SCE_KERNEL_ATTR_THREAD_PRIO, 1, 2, NULL);
    SceUID owner = make_thread("owner", owner_entry, OWNER_PRIORITY);
    const SceUID waiter = make_thread("waiter", waiter_entry, WAITER_PRIORITY);
    sceKernelStartThread(owner, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(owner, WAIT_TYPE_SEMA, sema));
    sceKernelStartThread(waiter, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(waiter, WAIT_TYPE_SEMA, sema));

    const int a_delete = sceKernelDeleteMutex(mutex);
    const int a_owner = current_of(owner);
    SceKernelSemaInfo info = { .size = sizeof(info) };
    sceKernelGetSemaInfo(sema, &info);
    const int a_count = info.currentCount;
    const int a_waiters = info.numWaitThreads;
    const int a_waiter_waits = test_in_wait(waiter, WAIT_TYPE_SEMA, sema);

    sceKernelSignalSema(sema, 2);
    test_join_thread(owner);
    test_join_thread(waiter);
    const int a_owner_wait = owner_wait;
    sceKernelDeleteSema(sema);

    // Case (b)
    mutex_b = make_ceiling_mutex("ceiling b", CEILING);
    mutex = make_ceiling_mutex("ceiling a", LOW_CEILING);
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    owner = make_thread("owner", two_owner_entry, OWNER_PRIORITY);
    sceKernelStartThread(owner, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(owner, WAIT_TYPE_SEMA, sema));
    sceKernelDeleteMutex(mutex);
    const int b_after_a = current_of(owner);
    sceKernelDeleteMutex(mutex_b);
    const int b_after_b = current_of(owner);

    sceKernelSignalSema(sema, 1);
    test_join_thread(owner);
    sceKernelDeleteSema(sema);

    // Case (c)
    mutex = make_ceiling_mutex("ceiling", CEILING);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    sema = sceKernelCreateSema("sema", 0, 0, 2, NULL);
    const SceUID y = make_thread("y", y_entry, OTHER_PRIORITY);
    owner = make_thread("owner", owner_entry, OWNER_PRIORITY);
    const SceUID x = make_thread("x", x_entry, OTHER_PRIORITY);
    sceKernelStartThread(y, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(y, WAIT_TYPE_COND, cond));
    sceKernelStartThread(owner, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(owner, WAIT_TYPE_SEMA, sema));
    sceKernelStartThread(x, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(x, WAIT_TYPE_MUTEX, mutex));

    sceKernelDeleteMutex(mutex);
    const int c_owner = current_of(owner);
    test_join_thread(y);
    test_join_thread(x);

    sceKernelSignalSema(sema, 2);
    test_join_thread(owner);

    PRINT("(a) the delete", a_delete);
    PRINT("(a) O's priority right after the delete", a_owner);
    PRINT("(a) S's count right after the delete", a_count);
    PRINT("(a) S's waiters right after the delete", a_waiters);
    PRINT("(a) W still waits right after the delete", a_waiter_waits);
    PRINT("(a) O's wait", a_owner_wait);
    PRINT("(a) W's wait", waiter_wait);
    PRINT("(b) O's priority after the delete of A", b_after_a);
    PRINT("(b) O's priority after the delete of B", b_after_b);
    PRINT("(c) Y's wait", y_wait);
    PRINT("(c) Y's priority after it", y_priority);
    PRINT("(c) X's lock", x_lock);
    PRINT("(c) X's priority after it", x_priority);
    PRINT("(c) O's priority after the delete", c_owner);

    sceKernelDeleteCond(cond);
    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
