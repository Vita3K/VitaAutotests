// A recursive lightweight mutex at the top of its count, 0x7FFFFFFF, and what a lock that adds to
// it does. Four cases, each on fresh mutexes:
// - a mutex created with a count of 0x7FFFFFFF, which its creator owns at that count. The creator
//   unlocks all of it, then another thread's try lock checks that the mutex is free.
// - a free recursive mutex locked with 0x7FFFFFFF, then a try lock of 1 that adds past the top.
// - the same with a lock of 1 instead of a try lock, on a second mutex.
// - the owner holds 0x7FFFFFFF, and a waiter asks for 0x7FFFFFFF with a 2 second timeout. The
//   owner unlocks all of it, which hands the mutex to the waiter. The waiter records what it holds,
//   then keeps it until the test lets it unlock again.
// Each step prints its result, the count word and the info count where the case needs them, and
// whether the info owner is the thread that holds the mutex. The mutexes of the wrap cases are
// deleted while their owner still holds them, and no thread waits on them then. The models are
// lw_zero_sum_recursive_owner, for the count word and the wrapped count, and lw_mutex, for the
// recursive counts.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceKernelLwMutexWork top, wrapped_try, wrapped_lock, contended;
static SceUID gate, waiter_thread;
static SceKernelLwMutexWork *target;
static volatile int other_try, waiter_lock, waiter_count, waiter_owner, waiter_word0;
static volatile int waiter_recorded, waiter_unlock;

static unsigned int word(const SceKernelLwMutexWork *work, int i) {
    return ((const volatile unsigned int *)work)[i];
}

static LwMutexInfo info_of(SceKernelLwMutexWork *work) {
    LwMutexInfo info = { .size = sizeof(info) };
    sceKernelGetLwMutexInfo(work, &info);
    return info;
}

// Tries to lock target with a count of 1, and unlocks it again if that worked
static int other_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    other_try = sceKernelTryLockLwMutex(target, 1);
    if (other_try == 0)
        sceKernelUnlockLwMutex(target, 1);
    return 0;
}

// Runs a try lock of work by another thread, which reports 0 if the mutex is free
static int try_by_other(SceKernelLwMutexWork *work) {
    target = work;
    test_join_thread(test_start_thread(other_thread));
    return other_try;
}

// Locks contended with 0x7FFFFFFF, records what it holds, and keeps it until the gate opens
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    waiter_lock = sceKernelLockLwMutex(&contended, 0x7FFFFFFF, &timeout);
    const LwMutexInfo info = info_of(&contended);
    waiter_count = info.currentCount;
    waiter_owner = info.currentOwnerId == sceKernelGetThreadId();
    waiter_word0 = word(&contended, 0) == (unsigned)sceKernelGetThreadId();
    waiter_recorded = 1;
    SceUInt32 gate_timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &gate_timeout);
    waiter_unlock = sceKernelUnlockLwMutex(&contended, 0x7FFFFFFF);
    return 0;
}

// Creates a mutex with a count of 0x7FFFFFFF, which its creator owns at that count, then unlocks it
static void created_case(void) {
    const SceUID owner = sceKernelGetThreadId();
    PRINT("created with 0x7FFFFFFF: create",
          sceKernelCreateLwMutex(&top, "top", LW_MUTEX_ATTR_RECURSIVE, 0x7FFFFFFF, NULL));
    const LwMutexInfo info = info_of(&top);
    PRINT("created with 0x7FFFFFFF: the info count", info.currentCount);
    PRINT("created with 0x7FFFFFFF: the init count", info.initCount);
    PRINT("created with 0x7FFFFFFF: the info owner is this thread", info.currentOwnerId == owner);
    PRINT("created with 0x7FFFFFFF: word 0 is the owner's UID", word(&top, 0) == (unsigned)owner);
    PRINT("created with 0x7FFFFFFF: word 2, the count", word(&top, 2));
    PRINT("unlock of 0x7FFFFFFF", sceKernelUnlockLwMutex(&top, 0x7FFFFFFF));
    PRINT("unlocked: the info count", info_of(&top).currentCount);
    PRINT("unlocked: word 2, the count", word(&top, 2));
    PRINT("unlocked: another thread's try lock", try_by_other(&top));
    PRINT("delete", sceKernelDeleteLwMutex(&top));
}

// Adds 1 to a count of 0x7FFFFFFF with a try lock or a lock, which wraps it, then unlocks both
static void wrap_case(SceKernelLwMutexWork *work, const char *name, int with_try) {
    const SceUID owner = sceKernelGetThreadId();
    SceUInt32 timeout = TEST_WAIT_US;
    PRINTF(sceKernelCreateLwMutex(work, "wrap", LW_MUTEX_ATTR_RECURSIVE, 0, NULL),
           "%s: create", name);
    PRINTF(sceKernelLockLwMutex(work, 0x7FFFFFFF, &timeout), "%s: lock of 0x7FFFFFFF", name);
    PRINTF(word(work, 2), "%s: word 2, the count", name);
    timeout = TEST_WAIT_US;
    if (with_try)
        PRINTF(sceKernelTryLockLwMutex(work, 1), "%s: try lock of 1", name);
    else
        PRINTF(sceKernelLockLwMutex(work, 1, &timeout), "%s: lock of 1", name);
    PRINTF(word(work, 2), "%s: word 2 after it, the count", name);
    PRINTF(info_of(work).currentCount < 0, "%s: the info count is negative", name);
    PRINTF(info_of(work).currentOwnerId == owner, "%s: the info owner is this thread", name);
    PRINTF(sceKernelUnlockLwMutex(work, 1), "%s: unlock of 1", name);
    PRINTF(sceKernelUnlockLwMutex(work, 0x7FFFFFFF), "%s: unlock of 0x7FFFFFFF", name);
    PRINTF(word(work, 2), "%s: word 2 after the unlocks, the count", name);
    PRINTF(info_of(work).currentCount < 0,
           "%s: the info count is negative after the unlocks", name);
    PRINTF(sceKernelDeleteLwMutex(work), "%s: delete", name);
}

// The owner holds 0x7FFFFFFF while a waiter asks for the same count, then the owner unlocks it all
static void handover_case(void) {
    SceUInt32 timeout = TEST_WAIT_US;
    PRINT("handover: create",
          sceKernelCreateLwMutex(&contended, "queue", LW_MUTEX_ATTR_RECURSIVE, 0, NULL));
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    PRINT("handover: lock of 0x7FFFFFFF by the owner",
          sceKernelLockLwMutex(&contended, 0x7FFFFFFF, &timeout));
    PRINT("handover: the owner's info count", info_of(&contended).currentCount);
    waiter_recorded = 0;
    waiter_thread = test_start_thread(waiter);
    test_wait_until_waiting(waiter_thread);
    PRINT("handover: unlock of 0x7FFFFFFF by the owner",
          sceKernelUnlockLwMutex(&contended, 0x7FFFFFFF));
    TEST_WAIT_FOR(waiter_recorded);
    PRINT("handover: the waiter's lock", waiter_lock);
    PRINT("handover: the waiter's info count", waiter_count);
    PRINT("handover: the waiter's info owner is this thread", waiter_owner);
    PRINT("handover: word 0 is the waiter's UID", waiter_word0);
    PRINT("handover: word 1, the wait count", word(&contended, 1));
    PRINT("handover: word 2, the count", word(&contended, 2));
    sceKernelSignalSema(gate, 1);
    test_join_thread(waiter_thread);
    PRINT("handover: the waiter's unlock of 0x7FFFFFFF", waiter_unlock);
    PRINT("handover: delete", sceKernelDeleteLwMutex(&contended));
    sceKernelDeleteSema(gate);
}

int main(void) {
    created_case();
    wrap_case(&wrapped_try, "try lock past the top", 1);
    wrap_case(&wrapped_lock, "lock past the top", 0);
    handover_case();

    test_exit();
    return 0;
}
