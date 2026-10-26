// What a reader/writer lock holds, and who gets it, when its write owner ends while readers and
// writers wait. The owner takes the write lock, once or twice, waits for go and ends with the lock
// held. The waiters queue behind it one at a time. They all run on core 0 below the main thread, so
// right after the owner has ended, the main thread reads the lock and each waiter's thread status
// before any waiter runs. Then the test releases the waiters that get the lock, one at a time, in
// the order the case gives. Each one keeps the lock until it is released, and the lock count is
// read right after the first release's unlock. The cases:
// - a: a FIFO queue of reader R1, writer W and reader R2. W gets the lock past R1, and R1 and R2
//   keep waiting.
// - b: a FIFO queue of readers R1 and R2. Both get the lock at once.
// - c: a priority queue with writer W at 0xB0 queued first, then reader R at 0xA0. W gets the lock
//   first, and R keeps waiting.
// - d: a recursive lock that the owner holds twice, and a FIFO queue of reader R then writer W. The
//   owner's lock is released whole, and W gets it.
//
// The test prints the lock count, the write owner (0 for none, k + 1 for the k-th waiter counting
// from 1), the waiting readers and writers, and whether each waiter is still queued for the lock
// (1) or has got it (0) after the owner ends. Then it prints each waiter's lock result, and the
// lock count after the first release's unlock. The raw thread status is not printed, since a waiter
// that got the lock can show as ready or as running, depending on when it first runs.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define HIGH_PRIORITY 0xA0
#define LOW_PRIORITY 0xB0
#define MAX_WAITERS 3
#define NOT_DONE 0xDEAD

typedef struct Waiter {
    const char *name;
    int write;
    int priority;
} Waiter;

// One case: the lock attr, how many times the owner locks, the waiters in queue order, and the
// order they are released in
typedef struct Case {
    const char *name;
    SceUInt32 attr;
    int owner_locks;
    const Waiter *waiters;
    int count;
    const int *release;
} Case;

static SceUID rwlock, go, threads[MAX_WAITERS], holds[MAX_WAITERS];
static const Waiter *waiters;
static int owner_locks;
static volatile int lock_result[MAX_WAITERS];

// Takes the write lock, owner_locks times, waits for go and ends with the lock held
static int owner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelLockWriteRWLock(rwlock, &timeout);
    if (owner_locks == 2) {
        timeout = TEST_WAIT_US;
        sceKernelLockWriteRWLock(rwlock, &timeout);
    }
    timeout = TEST_WAIT_US;
    sceKernelWaitSema(go, 1, &timeout);
    return 0;
}

// Takes the lock the waiter asks for, keeps it until its hold is released, then unlocks it
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = 3 * TEST_WAIT_US;
    if (waiters[i].write)
        lock_result[i] = sceKernelLockWriteRWLock(rwlock, &timeout);
    else
        lock_result[i] = sceKernelLockReadRWLock(rwlock, &timeout);
    if (lock_result[i] == 0) {
        timeout = 3 * TEST_WAIT_US;
        sceKernelWaitSema(holds[i], 1, &timeout);
        if (waiters[i].write)
            sceKernelUnlockWriteRWLock(rwlock);
        else
            sceKernelUnlockReadRWLock(rwlock);
    }
    return 0;
}

// Whether thread waits for a reader/writer lock, with or without callbacks
static int waits_for_rwlock(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && (info.status & SCE_THREAD_WAITING) &&
           (info.waitType & ~WAIT_TYPE_CB) == WAIT_TYPE_RW_LOCK;
}

// 0 for no write owner, i + 2 for waiter i, and 99 for any other thread
static int owner_code(SceUID owner, int count) {
    if (owner == 0)
        return 0;
    for (int i = 0; i < count; i++) {
        if (owner == threads[i])
            return i + 2;
    }
    return 99;
}

// Runs one case. The owner ends with the lock held and the waiters queue behind it. Then the
// holders are released in the case's order, and the test prints what it read.
static void run(const Case *c) {
    rwlock = sceKernelCreateRWLock("rwlock", c->attr, NULL);
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    waiters = c->waiters;
    owner_locks = c->owner_locks;
    for (int i = 0; i < c->count; i++) {
        holds[i] = sceKernelCreateSema("hold", 0, 0, 1, NULL);
        lock_result[i] = NOT_DONE;
    }
    const SceUID owner = sceKernelCreateThread(
        "owner", owner_entry, HIGH_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(owner, 0, NULL);
    test_wait_until_waiting(owner);
    for (int i = 0; i < c->count; i++) {
        threads[i] = sceKernelCreateThread("waiter", waiter_entry, c->waiters[i].priority, 0x4000,
                                           0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(threads[i], sizeof(i), &i);
        TEST_WAIT_FOR(waits_for_rwlock(threads[i]));
    }

    sceKernelSignalSema(go, 1);
    sceKernelWaitThreadEnd(owner, NULL, NULL);
    sceKernelDeleteThread(owner);

    // Read while the waiters are still ready or waiting, before the first of them runs. A waiter
    // that got the lock is not queued, whether it is ready or has run up to its hold.
    SceKernelRWLockInfo info = { .size = sizeof(info) };
    sceKernelGetRWLockInfo(rwlock, &info);
    const int lock_count = info.lockCount;
    const int owner_now = owner_code(info.writeOwnerId, c->count);
    const int readers = info.numReadWaitThreads;
    const int writers = info.numWriteWaitThreads;
    int queued[MAX_WAITERS];
    for (int i = 0; i < c->count; i++)
        queued[i] = waits_for_rwlock(threads[i]);

    // Each waiter that gets the lock holds it until its release, so the count after the first
    // release is the one its unlock leaves
    int after_first = -1;
    for (int k = 0; k < c->count; k++) {
        const int i = c->release[k];
        sceKernelSignalSema(holds[i], 1);
        test_join_thread(threads[i]);
        if (k == 0) {
            sceKernelGetRWLockInfo(rwlock, &info);
            after_first = info.lockCount;
        }
    }

    PRINTF(lock_count, "%s: lock count after the owner ends", c->name);
    PRINTF(owner_now, "%s: write owner after the owner ends (0 none, k + 1 for waiter k)",
           c->name);
    PRINTF(readers, "%s: waiting readers after the owner ends", c->name);
    PRINTF(writers, "%s: waiting writers after the owner ends", c->name);
    for (int i = 0; i < c->count; i++) {
        PRINTF(queued[i], "%s: %s still queued for the lock after the owner ends", c->name,
               c->waiters[i].name);
    }
    for (int i = 0; i < c->count; i++)
        PRINTF(lock_result[i], "%s: %s's lock result", c->name, c->waiters[i].name);
    PRINTF(after_first, "%s: lock count after the first release's unlock", c->name);

    for (int i = 0; i < c->count; i++)
        sceKernelDeleteSema(holds[i]);
    sceKernelDeleteSema(go);
    sceKernelDeleteRWLock(rwlock);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    static const Waiter r1_w_r2[] = {
        { "R1", 0, LOW_PRIORITY },
        { "W", 1, LOW_PRIORITY },
        { "R2", 0, LOW_PRIORITY },
    };
    static const int r1_w_r2_release[] = { 1, 0, 2 };
    static const Case case_a = { "a: FIFO R1, W, R2", 0, 1, r1_w_r2, 3, r1_w_r2_release };
    run(&case_a);

    static const Waiter two_readers[] = {
        { "R1", 0, LOW_PRIORITY },
        { "R2", 0, LOW_PRIORITY },
    };
    static const int two_readers_release[] = { 0, 1 };
    static const Case case_b = { "b: FIFO R1, R2", 0, 1, two_readers, 2, two_readers_release };
    run(&case_b);

    static const Waiter w_then_high_r[] = {
        { "W", 1, LOW_PRIORITY },
        { "R", 0, HIGH_PRIORITY },
    };
    static const int w_then_high_r_release[] = { 0, 1 };
    static const Case case_c = { "c: priority W, R (R higher)", SCE_KERNEL_ATTR_THREAD_PRIO, 1,
                                 w_then_high_r, 2, w_then_high_r_release };
    run(&case_c);

    static const Waiter r_then_w[] = {
        { "R", 0, LOW_PRIORITY },
        { "W", 1, LOW_PRIORITY },
    };
    static const int r_then_w_release[] = { 1, 0 };
    static const Case case_d = { "d: recursive owner, FIFO R, W", RW_LOCK_ATTR_RECURSIVE, 2,
                                 r_then_w, 2, r_then_w_release };
    run(&case_d);

    test_exit();
    return 0;
}
