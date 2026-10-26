// Whether a reader queued behind two writers gets its read lock when the first writer leaves the
// queue, and who gets the lock once the callback of that writer has run. The main thread read
// locks a FIFO rwlock, then three threads queue behind it in the order FIRST_W, SECOND_W and
// READER: two writers and a reader, with timeouts of 1 s, 3 s and 4 s. The main thread and every
// waiter run on core 0, the waiters below the main thread, so a woken waiter stays ready until the
// main thread blocks. Each case uses a new rwlock and new threads. The cases:
// - a: FIRST_W times out while SECOND_W still waits. The state is read right after FIRST_W left,
//   with READER still queued. Then SECOND_W times out, and the test checks whether READER gets its
//   read lock at that moment.
// - b: FIRST_W waits with callbacks. A notify takes it out of the queue while its callback runs,
//   and the callback blocks on a gate. The state is read during the callback. Then the gate opens,
//   and the main thread waits until FIRST_W waits again. Then the main thread unlocks. Each thread
//   that gets the lock keeps it until the main thread lets it go, so the order of the takers shows.
// Each case prints what the main thread read, then the results of the waiters.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
// Below the main thread, and the waiters queue in the order of their index
#define FIRST_W_PRIORITY 0xA0
#define SECOND_W_PRIORITY 0xA8
#define READER_PRIORITY 0xB0

// The timeouts of the waiters, so FIRST_W leaves first, then SECOND_W, and READER gets in after it
#define FIRST_W_US 1000000
#define SECOND_W_US 3000000
#define READER_US 4000000

// How long a holder keeps the lock, or the callback waits on its gate, if the test doesn't let it
// go first
#define HOLD_US 5000000

enum { FIRST_W, SECOND_W, READER, WAITERS };

static const int priorities[WAITERS] = { FIRST_W_PRIORITY, SECOND_W_PRIORITY, READER_PRIORITY };
static const SceUInt timeouts[WAITERS] = { FIRST_W_US, SECOND_W_US, READER_US };

static SceUID rwlock, go, gate;
static volatile SceUID callback;
static SceUID threads[WAITERS];
static volatile int use_cb, in_callback;
static volatile int result[WAITERS], got[WAITERS], done[WAITERS];
static volatile int takers, taken_by[WAITERS];

// Runs when FIRST_W is notified during its wait, and blocks on the gate until the test opens it
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    in_callback = 1;
    SceUInt timeout = HOLD_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// Waits for the lock, FIRST_W and SECOND_W for writing and READER for reading, then keeps it until
// the test lets it go
static int waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = timeouts[i];
    if (i == READER) {
        result[i] = sceKernelLockReadRWLock(rwlock, &timeout);
    } else if (use_cb && i == FIRST_W) {
        callback = sceKernelCreateCallback("cb", 0, callback_entry, NULL);
        result[i] = sceKernelLockWriteRWLockCB(rwlock, &timeout);
        sceKernelDeleteCallback(callback);
    } else {
        result[i] = sceKernelLockWriteRWLock(rwlock, &timeout);
    }
    if (result[i] == 0) {
        got[i] = 1;
        taken_by[takers] = i + 1;
        takers++;
        SceUInt hold = HOLD_US;
        sceKernelWaitSema(go, 1, &hold);
        if (i == READER)
            sceKernelUnlockReadRWLock(rwlock);
        else
            sceKernelUnlockWriteRWLock(rwlock);
    }
    done[i] = 1;
    return 0;
}

// Starts waiter i on core 0 at its priority, passing it its index
static SceUID start(int i) {
    const SceUID thread = sceKernelCreateThread(
        "waiter", waiter, priorities[i], 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, sizeof(i), &i);
    return thread;
}

// Starts the waiters in queue order, each once the one before it waits
static void queue_waiters(void) {
    for (int i = 0; i < WAITERS; i++) {
        threads[i] = start(i);
        test_wait_until_waiting(threads[i]);
    }
}

// Clears the state of a case
static void reset_state(void) {
    in_callback = takers = 0;
    for (int i = 0; i < WAITERS; i++) {
        result[i] = 0xDEAD;
        got[i] = done[i] = taken_by[i] = 0;
    }
}

// Returns the info of the rwlock
static SceKernelRWLockInfo lock_info(void) {
    SceKernelRWLockInfo info = { .size = sizeof(info) };
    sceKernelGetRWLockInfo(rwlock, &info);
    return info;
}

// Returns the wait type a thread reports
static SceUInt32 wait_type(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

// Waits up to 500 ms for READER to get its read lock, for an outcome that would come at once
static int reader_got_soon(void) {
    for (int i = 0; i < 500 && !got[READER]; i++)
        sceKernelDelayThread(1000);
    return got[READER];
}

// FIRST_W times out, then SECOND_W does
static void timeout_case(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    go = sceKernelCreateSema("go", 0, 0, WAITERS, NULL);
    sceKernelLockReadRWLock(rwlock, NULL);
    reset_state();
    use_cb = 0;
    queue_waiters();

    TEST_WAIT_FOR(lock_info().numWriteWaitThreads == 1);
    SceKernelRWLockInfo info = lock_info();
    PRINT("a: after FIRST_W left, waiting writers", info.numWriteWaitThreads);
    PRINT("a: after FIRST_W left, waiting readers", info.numReadWaitThreads);
    PRINT("a: after FIRST_W left, lock count", info.lockCount);
    PRINT("a: after FIRST_W left, READER got its read lock", got[READER]);
    PRINT("a: after FIRST_W left, READER's wait type", wait_type(threads[READER]));

    TEST_WAIT_FOR(done[SECOND_W]);
    PRINT("a: after SECOND_W left, READER got its read lock within 500 ms", reader_got_soon());
    info = lock_info();
    PRINT("a: after SECOND_W left, lock count", info.lockCount);
    test_join_thread(threads[FIRST_W]);
    test_join_thread(threads[SECOND_W]);
    PRINT("a: FIRST_W's result", result[FIRST_W]);
    PRINT("a: SECOND_W's result", result[SECOND_W]);

    sceKernelUnlockReadRWLock(rwlock);
    sceKernelSignalSema(go, 1);
    test_join_thread(threads[READER]);
    PRINT("a: READER's result", result[READER]);
    sceKernelDeleteSema(go);
    sceKernelDeleteRWLock(rwlock);
}

// FIRST_W waits with callbacks, and a notify runs its callback while it is out of the queue
static void callback_case(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    go = sceKernelCreateSema("go", 0, 0, WAITERS, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    sceKernelLockReadRWLock(rwlock, NULL);
    reset_state();
    use_cb = 1;
    queue_waiters();

    PRINT("b: notify FIRST_W's callback", sceKernelNotifyCallback(callback, 1));
    TEST_WAIT_FOR(in_callback);
    SceKernelRWLockInfo info = lock_info();
    PRINT("b: during the callback, the callback ran", in_callback);
    PRINT("b: during the callback, waiting writers", info.numWriteWaitThreads);
    PRINT("b: during the callback, waiting readers", info.numReadWaitThreads);
    PRINT("b: during the callback, lock count", info.lockCount);
    PRINT("b: during the callback, READER's wait type", wait_type(threads[READER]));
    PRINT("b: during the callback, READER got its read lock", got[READER]);

    sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(lock_info().numWriteWaitThreads == 2);
    PRINT("b: after the callback, FIRST_W waits again, waiting writers",
          lock_info().numWriteWaitThreads);
    PRINT("b: the main thread's read unlock", sceKernelUnlockReadRWLock(rwlock));

    // Lets each taker go once it has the lock, so the next one is served
    for (int k = 0; k < WAITERS; k++) {
        TEST_WAIT_FOR(takers > k);
        sceKernelSignalSema(go, 1);
    }
    for (int i = 0; i < WAITERS; i++)
        test_join_thread(threads[i]);
    for (int k = 0; k < WAITERS; k++)
        PRINTF(taken_by[k], "b: taker %d after the read unlock (1 FIRST_W, 2 SECOND_W, 3 READER)",
               k + 1);
    PRINT("b: FIRST_W's result", result[FIRST_W]);
    PRINT("b: SECOND_W's result", result[SECOND_W]);
    PRINT("b: READER's result", result[READER]);
    sceKernelDeleteSema(gate);
    sceKernelDeleteSema(go);
    sceKernelDeleteRWLock(rwlock);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    timeout_case();
    callback_case();

    test_exit();
    return 0;
}
