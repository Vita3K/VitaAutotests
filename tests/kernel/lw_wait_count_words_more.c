// The wait count in word 1 of a lightweight mutex's work area, with several threads queued. The
// main thread holds a recursive mutex once, and three waiters queue for it in FIFO order: W1 wants
// 1, W2 wants 2 and W3 wants 1. The test prints the words and the waiter count after each one
// queues, then the main thread locks again, which stays in user code, and a third thread's try
// lock fails. The main thread's unlock of 1 keeps the mutex, and its second unlock hands it to W1.
// Each waiter holds the mutex at a gate, and the test prints the words while it holds it, then
// lets it unlock, so the hand overs to W2 and W3 are printed too. Last, a thread's two lock calls
// with a timeout of 0 and its try lock fail on the held mutex, a waiter W4 queues behind them and
// gets the mutex in a hand over, and a timeout 0 lock that is followed by an unlock with no waiters
// leaves a stale wait count that a later lock, unlock and try lock by another thread keep.
//
// The cases:
// - W1, W2 and W3 queue behind the main thread, which holds the mutex once
// - the main thread locks again, and a third thread's try lock fails
// - the main thread's unlock of 1, then its unlock of 1, which hands the mutex to W1
// - W1, W2 and W3 hold the mutex in turn at the gate and unlock it, each hand over to the next
// - a thread's two locks with a timeout of 0 and a try lock, all while the main thread holds it
// - W4 queues, and the main thread's unlock hands the mutex over to it
// - a timeout 0 lock, then the main thread's unlock with no waiters, which leaves the wait count
// - the main thread's lock and unlock, then a try lock by another thread and its unlock
//
// The waiters run below the main thread, all pinned to core 0, so none of them runs between a call
// and the reads after it. Every queued waiter waits at most 5 s.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0

// How long a queued waiter waits at most, long enough that no step of the main thread times it out
#define QUEUED_WAIT_US 5000000

// One slot per thread of the test, each with its own results
#define SLOTS 10

static SceKernelLwMutexWork mutex;
static SceUID gate;
static volatile int lock_result[SLOTS], unlock_result[SLOTS], want[SLOTS];

// Locks the mutex with the count of its slot, holds it at the gate, then unlocks it
static int holder(SceSize args, void *argp) {
    (void)args;
    const int slot = *(const int *)argp;
    SceUInt32 timeout = QUEUED_WAIT_US;
    lock_result[slot] = sceKernelLockLwMutex(&mutex, want[slot], &timeout);
    if (lock_result[slot] == 0) {
        timeout = QUEUED_WAIT_US;
        sceKernelWaitSema(gate, 1, &timeout);
        unlock_result[slot] = sceKernelUnlockLwMutex(&mutex, want[slot]);
    }
    return 0;
}

// Try locks the mutex, and if it gets it, holds it at the gate, then unlocks it
static int try_holder(SceSize args, void *argp) {
    (void)args;
    const int slot = *(const int *)argp;
    lock_result[slot] = sceKernelTryLockLwMutex(&mutex, 1);
    if (lock_result[slot] == 0) {
        SceUInt32 timeout = QUEUED_WAIT_US;
        sceKernelWaitSema(gate, 1, &timeout);
        unlock_result[slot] = sceKernelUnlockLwMutex(&mutex, 1);
    }
    return 0;
}

// Locks the mutex with a timeout of 0, and unlocks it if it got it
static int zero_locker(SceSize args, void *argp) {
    (void)args;
    const int slot = *(const int *)argp;
    SceUInt32 timeout = 0;
    lock_result[slot] = sceKernelLockLwMutex(&mutex, 1, &timeout);
    if (lock_result[slot] == 0)
        unlock_result[slot] = sceKernelUnlockLwMutex(&mutex, 1);
    return 0;
}

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

static unsigned int word(int i) {
    return ((const volatile unsigned int *)&mutex)[i];
}

static SceUInt32 waiters(void) {
    LwMutexInfo info = { .size = sizeof(info) };
    sceKernelGetLwMutexInfo(&mutex, &info);
    return info.numWaitThreads;
}

// Prints the words of the work area and the waiter count, with word 0 compared to owner
static void print_words(const char *step, SceUID owner) {
    PRINTF(word(0) == (unsigned int)owner, "%s: word 0 is the expected owner", step);
    PRINTF(word(1), "%s: word 1", step);
    PRINTF(word(2), "%s: word 2", step);
    PRINTF(waiters(), "%s: the waiters", step);
}

// Starts entry on a thread pinned to core 0 below the main thread, with slot as its argument
static SceUID start_pinned(SceKernelThreadEntry entry, int slot) {
    const SceUID thread = sceKernelCreateThread(
        "waiter", entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, sizeof(slot), &slot);
    return thread;
}

// Starts a holder that locks the mutex with count, so it queues while the mutex is held
static SceUID start_holder(int slot, int count) {
    want[slot] = count;
    return start_pinned(holder, slot);
}

// Runs a try lock on a new thread and waits for it to end
static void run_try(int slot) {
    test_join_thread(start_pinned(try_holder, slot));
}

// Runs a lock with a timeout of 0 on a new thread and waits for it to end
static void run_zero(int slot) {
    test_join_thread(start_pinned(zero_locker, slot));
}

int main(void) {
    const SceUID main_thread = sceKernelGetThreadId();
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelCreateLwMutex(&mutex, "mutex", LW_MUTEX_ATTR_RECURSIVE, 0, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);

    // The main thread holds the mutex once, and W1, W2 and W3 queue for it in that order
    SceUInt32 timeout = TEST_WAIT_US;
    PRINT("the main thread's lock", sceKernelLockLwMutex(&mutex, 1, &timeout));
    print_words("the main thread holds it", main_thread);
    const SceUID w1 = start_holder(0, 1);
    TEST_WAIT_FOR(wait_type_of(w1) == WAIT_TYPE_LW_MUTEX);
    print_words("W1 queued", main_thread);
    const SceUID w2 = start_holder(1, 2);
    TEST_WAIT_FOR(wait_type_of(w2) == WAIT_TYPE_LW_MUTEX);
    print_words("W2 queued", main_thread);
    const SceUID w3 = start_holder(2, 1);
    TEST_WAIT_FOR(wait_type_of(w3) == WAIT_TYPE_LW_MUTEX);
    print_words("W3 queued", main_thread);

    // The owner's lock stays in user code, and a third thread's try lock fails
    PRINT("the main thread locks again", sceKernelLockLwMutex(&mutex, 1, &timeout));
    print_words("the main thread locks again", main_thread);
    run_try(3);
    PRINT("a third thread's try lock", lock_result[3]);
    print_words("after the third thread's try lock", main_thread);
    PRINT("the main thread's unlock of 1", sceKernelUnlockLwMutex(&mutex, 1));
    print_words("after the unlock of 1", main_thread);

    // The second unlock hands the mutex to W1, which holds it at the gate
    PRINT("the main thread's unlock of 1 again", sceKernelUnlockLwMutex(&mutex, 1));
    TEST_WAIT_FOR(wait_type_of(w1) == WAIT_TYPE_SEMA);
    print_words("W1 holds it", w1);
    sceKernelSignalSema(gate, 1);
    test_join_thread(w1);
    PRINT("W1's lock", lock_result[0]);
    PRINT("W1's unlock", unlock_result[0]);
    TEST_WAIT_FOR(wait_type_of(w2) == WAIT_TYPE_SEMA);
    print_words("W2 holds it", w2);
    sceKernelSignalSema(gate, 1);
    test_join_thread(w2);
    PRINT("W2's lock", lock_result[1]);
    PRINT("W2's unlock", unlock_result[1]);
    TEST_WAIT_FOR(wait_type_of(w3) == WAIT_TYPE_SEMA);
    print_words("W3 holds it", w3);
    sceKernelSignalSema(gate, 1);
    test_join_thread(w3);
    PRINT("W3's lock", lock_result[2]);
    PRINT("W3's unlock", unlock_result[2]);
    print_words("after W3 unlocks", 0);

    // Two lock calls with a timeout of 0 and a try lock fail on the held mutex, and only the
    // timeouts raise the wait count
    PRINT("the main thread's lock, round two", sceKernelLockLwMutex(&mutex, 1, &timeout));
    print_words("the main thread holds it, round two", main_thread);
    run_zero(4);
    PRINT("a lock with a timeout of 0", lock_result[4]);
    print_words("after the lock with a timeout of 0", main_thread);
    run_zero(5);
    PRINT("a second lock with a timeout of 0", lock_result[5]);
    print_words("after the second lock with a timeout of 0", main_thread);
    run_try(6);
    PRINT("a try lock", lock_result[6]);
    print_words("after the try lock", main_thread);

    // W4 queues, and the main thread's unlock hands the mutex to it, which rewrites the wait count
    const SceUID w4 = start_holder(7, 1);
    TEST_WAIT_FOR(wait_type_of(w4) == WAIT_TYPE_LW_MUTEX);
    print_words("W4 queued", main_thread);
    PRINT("the main thread's unlock, round two", sceKernelUnlockLwMutex(&mutex, 1));
    TEST_WAIT_FOR(wait_type_of(w4) == WAIT_TYPE_SEMA);
    print_words("W4 holds it", w4);
    sceKernelSignalSema(gate, 1);
    test_join_thread(w4);
    PRINT("W4's lock", lock_result[7]);
    PRINT("W4's unlock", unlock_result[7]);
    print_words("after W4 unlocks", 0);

    // A lock with a timeout of 0 leaves a stale wait count, which an unlock with no waiters keeps
    PRINT("the main thread's lock, round three", sceKernelLockLwMutex(&mutex, 1, &timeout));
    run_zero(8);
    PRINT("a lock with a timeout of 0, round three", lock_result[8]);
    print_words("after that lock with a timeout of 0", main_thread);
    PRINT("the main thread's unlock, round three", sceKernelUnlockLwMutex(&mutex, 1));
    print_words("after the unlock with no waiters", 0);
    PRINT("the main thread's lock, round three again", sceKernelLockLwMutex(&mutex, 1, &timeout));
    print_words("the main thread holds it, round three", main_thread);
    PRINT("the main thread's unlock, round three again", sceKernelUnlockLwMutex(&mutex, 1));
    print_words("after the second unlock with no waiters", 0);

    // Another thread's try lock takes the free mutex, and it holds it at the gate
    const SceUID p = start_pinned(try_holder, 9);
    TEST_WAIT_FOR(wait_type_of(p) == WAIT_TYPE_SEMA);
    print_words("a try lock by another thread holds it", p);
    sceKernelSignalSema(gate, 1);
    test_join_thread(p);
    PRINT("the other thread's try lock", lock_result[9]);
    PRINT("the other thread's unlock", unlock_result[9]);
    print_words("after the other thread's unlock", 0);

    sceKernelDeleteLwMutex(&mutex);
    sceKernelDeleteSema(gate);
    test_exit();
    return 0;
}
