// The owner's own lock and try lock calls on a mutex while another thread waits to lock it. In each
// case the main thread owns a fresh mutex for a count of 1, and a waiter W waits to lock it for a
// count of 1. W holds the mutex at a gate once it gets it, and the test lets it go at the end. The
// test prints the count, the owner (0 for none, 1 for the main thread, 2 for W), the waiting thread
// count and W's wait type after each step, and each call's result. The cases:
// - a recursive mutex. The main thread locks it to a count of 2 with a timeout of 0, try locks it
//   to a count of 1, try locks it to 0x7FFFFFFF, which overflows the count, then unlocks it by 3,
//   which leaves W waiting, then unlocks it by 1, which hands it to W with a count of 1. The main
//   thread's try lock after that fails. The test checks that the owner's calls add to the count and
//   leave the queue alone.
// - a plain mutex. The main thread's lock with a timeout of 0 and its try lock fail with
//   0x80028142 and leave the count and the waiting thread count at 1. A third thread's try lock
//   fails with 0x80028145 and its lock with a timeout of 0 fails with 0x80028005. The main thread's
//   unlock then hands the mutex to W.
// - a recursive mutex, with a timeout variable of 500 ms for each lock. The lock to 2 and the lock
//   to 1 add to the count at once, and the test prints whether each timeout variable is unchanged.
//   Then the main thread unlocks it by 4, which hands it to W.
// Every thread is pinned to core 0, and W runs below the main thread.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0

static SceUID mutex, gate, main_thread, waiter_uid, other_uid;
static volatile int waiter_result = 0xDEAD, waiter_unlock = 0xDEAD;
static volatile int other_try = 0xDEAD, other_lock = 0xDEAD;

// Locks the mutex for a count of 1, holds it until the gate is signaled, then unlocks it
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    waiter_result = sceKernelLockMutex(mutex, 1, &timeout);
    if (waiter_result == 0) {
        timeout = TEST_WAIT_US;
        sceKernelWaitSema(gate, 1, &timeout);
        waiter_unlock = sceKernelUnlockMutex(mutex, 1);
    }
    return 0;
}

// Tries the mutex, then locks it with a timeout of 0
static int other_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    other_try = sceKernelTryLockMutex(mutex, 1);
    SceUInt timeout = 0;
    other_lock = sceKernelLockMutex(mutex, 1, &timeout);
    return 0;
}

// Creates a thread on core 0 below the main thread and starts it
static SceUID start_low_thread(const char *name, SceKernelThreadEntry entry) {
    const SceUID thread =
        sceKernelCreateThread(name, entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

// Codes the owner: 0 for none, 1 for the main thread and 2 for another thread
static int owner_code(SceUID owner) {
    return owner == 0 ? 0 : owner == main_thread ? 1 : 2;
}

// Prints the count, owner, waiting thread count and W's wait type after a step
static void print_state(const char *step) {
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    PRINTF(info.currentCount, "%s: the count", step);
    PRINTF(owner_code(info.currentOwnerId), "%s: the owner", step);
    PRINTF(info.numWaitThreads, "%s: the waiting threads", step);
    PRINTF(wait_type_of(waiter_uid), "%s: W's wait type", step);
}

// Creates the mutex with attr, locks it for the main thread, and queues W behind it
static void queue_waiter(SceUInt attr, const char *label) {
    mutex = sceKernelCreateMutex("mutex", attr, 0, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    waiter_result = waiter_unlock = 0xDEAD;
    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelLockMutex(mutex, 1, &timeout), "%s: the first lock", label);
    waiter_uid = start_low_thread("waiter", waiter);
    TEST_WAIT_FOR(wait_type_of(waiter_uid) == WAIT_TYPE_MUTEX);
}

// Waits until W owns the mutex and holds it at the gate, so its state can be read while blocked
static void wait_for_hold(void) {
    TEST_WAIT_FOR(wait_type_of(waiter_uid) == WAIT_TYPE_SEMA);
}

// Lets W go on from the gate, then prints its lock and unlock results and deletes the objects
static void finish(const char *label) {
    sceKernelSignalSema(gate, 1);
    test_join_thread(waiter_uid);
    PRINTF(waiter_result, "%s: W's lock", label);
    PRINTF(waiter_unlock, "%s: W's unlock", label);
    sceKernelDeleteMutex(mutex);
    sceKernelDeleteSema(gate);
}

static void recursive_case(void) {
    queue_waiter(SCE_KERNEL_MUTEX_ATTR_RECURSIVE, "recursive");
    print_state("recursive: W queued");
    SceUInt timeout = 0;
    PRINT("recursive: lock to 2 with a timeout of 0", sceKernelLockMutex(mutex, 2, &timeout));
    print_state("recursive: after the lock to 2");
    PRINT("recursive: try lock to 1", sceKernelTryLockMutex(mutex, 1));
    print_state("recursive: after the try lock to 1");
    PRINT("recursive: try lock to 0x7FFFFFFF", sceKernelTryLockMutex(mutex, 0x7FFFFFFF));
    print_state("recursive: after the try lock to 0x7FFFFFFF");
    PRINT("recursive: unlock by 3", sceKernelUnlockMutex(mutex, 3));
    print_state("recursive: after the unlock by 3");
    PRINT("recursive: unlock by 1", sceKernelUnlockMutex(mutex, 1));
    wait_for_hold();
    print_state("recursive: after the unlock by 1");
    PRINT("recursive: the main thread's try lock", sceKernelTryLockMutex(mutex, 1));
    finish("recursive");
}

static void plain_case(void) {
    queue_waiter(0, "plain");
    print_state("plain: W queued");
    SceUInt timeout = 0;
    PRINT("plain: lock with a timeout of 0", sceKernelLockMutex(mutex, 1, &timeout));
    print_state("plain: after the lock");
    PRINT("plain: try lock", sceKernelTryLockMutex(mutex, 1));
    print_state("plain: after the try lock");
    other_uid = start_low_thread("other", other_thread);
    test_join_thread(other_uid);
    PRINT("plain: a third thread's try lock", other_try);
    PRINT("plain: a third thread's lock with a timeout of 0", other_lock);
    print_state("plain: after the third thread");
    PRINT("plain: unlock", sceKernelUnlockMutex(mutex, 1));
    wait_for_hold();
    print_state("plain: after the unlock");
    finish("plain");
}

static void timeout_case(void) {
    queue_waiter(SCE_KERNEL_MUTEX_ATTR_RECURSIVE, "timeout");
    print_state("timeout: W queued");
    SceUInt timeout = 500000;
    PRINT("timeout: lock to 2 with a timeout of 500 ms", sceKernelLockMutex(mutex, 2, &timeout));
    PRINT("timeout: the timeout is unchanged", timeout == 500000);
    print_state("timeout: after the lock to 2");
    timeout = 500000;
    PRINT("timeout: lock to 1 with a timeout of 500 ms", sceKernelLockMutex(mutex, 1, &timeout));
    PRINT("timeout: the timeout is unchanged after it", timeout == 500000);
    print_state("timeout: after the lock to 1");
    PRINT("timeout: unlock by 4", sceKernelUnlockMutex(mutex, 4));
    wait_for_hold();
    print_state("timeout: after the unlock by 4");
    finish("timeout");
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);
    main_thread = sceKernelGetThreadId();

    recursive_case();
    plain_case();
    timeout_case();

    test_exit();
    return 0;
}
