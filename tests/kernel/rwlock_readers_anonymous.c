// Whether the read locks of a reader/writer lock belong to a thread. They are only a count, so any
// thread can release one, a writer gets the lock when the count reaches 0 whoever released the
// last one, and the holders' later unlocks fail. The main thread read locks twice, a helper reads
// locks once and ends, and a writer W queues for the write lock. Every thread runs on core 0 below
// the main thread, so a woken thread stays ready until the main thread blocks. Each case uses a
// new rwlock and new threads. The cases:
// - a: a thread X that holds no read lock unlocks four times. Its three unlocks take the count to
//   0, which gives W the write lock. Its fourth unlock, and the main thread's unlock, fail because
//   W owns the lock. After W unlocks, the main thread's unlock fails because nothing is locked.
// - b: a thread T holds one read lock and waits for the write lock with a timeout. The main thread,
//   which holds no lock, unlocks once, which gives T the write lock. T's read unlock fails while it
//   owns the write lock, and after its write unlock, its read unlock fails too.
// Each case prints each result, and the lock count, the owner and the waiter counts after each
// step. The owner prints as 0 for none, 1 for the main thread and 2 for another thread.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define THREAD_PRIORITY 0xA0

// How long a thread that got its lock keeps it, if the main thread doesn't let it go first
#define HOLD_US 5000000

static SceUID rwlock, go, main_thread;
static volatile int helper_result, writer_result, writer_unlock;
static volatile int x_results[4], holder_read, holder_write, holder_unlocks[3];

// Starts entry on core 0 at THREAD_PRIORITY
static SceUID start(SceKernelThreadEntry entry) {
    const SceUID started = sceKernelCreateThread(
        "t", entry, THREAD_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(started, 0, NULL);
    return started;
}

// Keeps the lock the thread got until the main thread signals go, or until HOLD_US passes
static void hold(void) {
    SceUInt timeout = HOLD_US;
    sceKernelWaitSema(go, 1, &timeout);
}

// Prints what the lock's info shows. The write owner prints as 0 for none, 1 for the main thread
// and 2 for another thread.
static void print_state(const char *step) {
    SceKernelRWLockInfo info = { .size = sizeof(info) };
    const int result = sceKernelGetRWLockInfo(rwlock, &info);
    if (result < 0) {
        PRINTF(result, "%s: info", step);
        return;
    }
    const int owner = info.writeOwnerId == 0 ? 0 : info.writeOwnerId == main_thread ? 1 : 2;
    PRINTF(info.lockCount, "%s: lock count", step);
    PRINTF(owner, "%s: write owner", step);
    PRINTF(info.numReadWaitThreads, "%s: waiting readers", step);
    PRINTF(info.numWriteWaitThreads, "%s: waiting writers", step);
}

// Read locks once, then ends while still holding the lock
static int helper_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    helper_result = sceKernelLockReadRWLock(rwlock, NULL);
    return 0;
}

// Waits for the write lock, then unlocks it once the test lets it go
static int writer_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    writer_result = sceKernelLockWriteRWLock(rwlock, &timeout);
    if (writer_result == 0) {
        hold();
        writer_unlock = sceKernelUnlockWriteRWLock(rwlock);
    }
    return 0;
}

// Unlocks the read lock four times, holding no read lock itself
static int unlocker_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (int i = 0; i < 4; i++)
        x_results[i] = sceKernelUnlockReadRWLock(rwlock);
    return 0;
}

// Holds a read lock, waits for the write lock, and unlocks in each mode once it has the write lock
static int holder_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    holder_read = sceKernelLockReadRWLock(rwlock, NULL);
    SceUInt timeout = TEST_WAIT_US;
    holder_write = sceKernelLockWriteRWLock(rwlock, &timeout);
    if (holder_write == 0) {
        hold();
        holder_unlocks[0] = sceKernelUnlockReadRWLock(rwlock);
        holder_unlocks[1] = sceKernelUnlockWriteRWLock(rwlock);
        holder_unlocks[2] = sceKernelUnlockReadRWLock(rwlock);
    }
    return 0;
}

// Read locks held by a thread and by the main thread, released by a thread that holds none
static void anonymous_case(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    sceKernelLockReadRWLock(rwlock, NULL);
    sceKernelLockReadRWLock(rwlock, NULL);
    print_state("a: main read locked twice");

    test_join_thread(start(helper_entry));
    PRINT("a: the helper's read lock", helper_result);
    print_state("a: after the helper read locked");

    const SceUID writer = start(writer_entry);
    TEST_WAIT_FOR(test_in_wait(writer, WAIT_TYPE_RW_LOCK, rwlock));
    print_state("a: with W waiting for the write lock");

    test_join_thread(start(unlocker_entry));
    PRINT("a: X's read unlock 1", x_results[0]);
    PRINT("a: X's read unlock 2", x_results[1]);
    PRINT("a: X's read unlock 3, which gives W the lock", x_results[2]);
    PRINT("a: X's read unlock 4", x_results[3]);
    print_state("a: after X's unlocks");
    PRINT("a: the main thread's read unlock", sceKernelUnlockReadRWLock(rwlock));
    print_state("a: after the main thread's read unlock");

    sceKernelSignalSema(go, 1);
    test_join_thread(writer);
    PRINT("a: W's write lock", writer_result);
    PRINT("a: W's write unlock", writer_unlock);
    PRINT("a: the main thread's read unlock after W's unlock", sceKernelUnlockReadRWLock(rwlock));
    print_state("a: at the end");
    sceKernelDeleteSema(go);
    sceKernelDeleteRWLock(rwlock);
}

// A thread that holds a read lock waits for the write lock, which a non-holder's unlock gives it
static void holder_case(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    const SceUID holder = start(holder_entry);
    TEST_WAIT_FOR(test_in_wait(holder, WAIT_TYPE_RW_LOCK, rwlock));
    print_state("b: T holds a read lock and waits for the write lock");

    PRINT("b: the main thread's read unlock", sceKernelUnlockReadRWLock(rwlock));
    print_state("b: after the main thread's read unlock");

    sceKernelSignalSema(go, 1);
    test_join_thread(holder);
    PRINT("b: T's read lock", holder_read);
    PRINT("b: T's write lock", holder_write);
    PRINT("b: T's read unlock while it owns the write lock", holder_unlocks[0]);
    PRINT("b: T's write unlock", holder_unlocks[1]);
    PRINT("b: T's read unlock after its write unlock", holder_unlocks[2]);
    print_state("b: at the end");
    sceKernelDeleteSema(go);
    sceKernelDeleteRWLock(rwlock);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);
    main_thread = sceKernelGetThreadId();

    anonymous_case();
    holder_case();

    test_exit();
    return 0;
}
