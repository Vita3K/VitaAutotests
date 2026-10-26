// A lightweight mutex deleted while another thread holds it. No thread waits on the mutex at the
// delete, so the delete is safe. The work area keeps the owner, the wait count and the count after
// the delete, and the calls on the work area see what is left. The cases:
// - the other thread holds the mutex at a gate, and the main thread deletes it
// - after the delete, a try lock, a lock with a 2 s timeout timed against 1 s, a lock with a CB
//   variant, a lock with a count of 0, a try lock with a count of 0, an unlock by a thread that
//   doesn't own it, an unlock with a count of 0, and info by work area
// - the other thread's unlock after the delete, then the main thread locks the deleted work area
//   and unlocks it
// - a second mutex held by the main thread while a thread waits 200 ms for it and gives up, so the
//   wait count of its work area is raised, then the main thread deletes the mutex and unlocks it,
//   and the words of the work area and a try lock by the main thread show what is left
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceKernelLwMutexWork held, timed;
static SceUID gate;
static volatile unsigned int main_uid;
static volatile int holder_lock = 0xDEAD, holder_unlock = 0xDEAD, waiter_result = 0xDEAD;

// Locks held, then holds it until the gate is signaled and unlocks it
static int holder_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    holder_lock = sceKernelLockLwMutex(&held, 1, &timeout);
    timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    holder_unlock = sceKernelUnlockLwMutex(&held, 1);
    return 0;
}

// Waits for timed for 200 ms and records the result
static int short_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 200000;
    waiter_result = sceKernelLockLwMutex(&timed, 1, &timeout);
    return 0;
}

// The word of a work area with the given index, as an unsigned value
static unsigned int word_of(const void *work, int i) {
    return ((const volatile unsigned int *)work)[i];
}

int main(void) {
    main_uid = (unsigned int)sceKernelGetThreadId();

    // Part one: the other thread holds the mutex when the main thread deletes it
    sceKernelCreateLwMutex(&held, "held", 0, 0, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    const SceUID holder = test_start_thread(holder_entry);
    TEST_WAIT_FOR(test_in_wait(holder, WAIT_TYPE_SEMA, gate));
    PRINT("the other thread's lock before the delete", holder_lock);
    PRINT("delete while another thread holds it", sceKernelDeleteLwMutex(&held));

    PRINT("try lock 1 after the delete", sceKernelTryLockLwMutex(&held, 1));

    SceUInt32 timeout = TEST_WAIT_US;
    const SceUInt64 lock_start = sceKernelGetProcessTimeWide();
    const int lock_result = sceKernelLockLwMutex(&held, 1, &timeout);
    const SceUInt64 lock_elapsed = sceKernelGetProcessTimeWide() - lock_start;
    PRINT("lock 1 with a 2 s timeout after the delete", lock_result);
    PRINTF(lock_elapsed < 1000000, "lock 1 with a 2 s timeout returned in under 1 s");

    timeout = TEST_WAIT_US;
    PRINT("lock CB 1 after the delete", sceKernelLockLwMutexCB(&held, 1, &timeout));
    timeout = TEST_WAIT_US;
    PRINT("lock 0 with a timeout after the delete", sceKernelLockLwMutex(&held, 0, &timeout));
    PRINT("try lock 0 after the delete", sceKernelTryLockLwMutex(&held, 0));
    PRINT("unlock 1 by a thread that doesn't own it", sceKernelUnlockLwMutex(&held, 1));
    PRINT("unlock 0 after the delete", sceKernelUnlockLwMutex(&held, 0));
    LwMutexInfo info = { .size = sizeof(info) };
    PRINT("info by work area after the delete", sceKernelGetLwMutexInfo(&held, &info));

    // The other thread unlocks after the delete, then the main thread takes the work area again
    sceKernelSignalSema(gate, 1);
    test_join_thread(holder);
    PRINT("the other thread's unlock after the delete", holder_unlock);
    timeout = TEST_WAIT_US;
    PRINT("lock 1 after the other thread's unlock", sceKernelLockLwMutex(&held, 1, &timeout));
    PRINT("unlock 1 after that lock", sceKernelUnlockLwMutex(&held, 1));

    // Part two: the main thread holds a second mutex while a waiter gives up, then deletes it
    sceKernelCreateLwMutex(&timed, "timed", 0, 0, NULL);
    timeout = TEST_WAIT_US;
    PRINT("main thread locks the second mutex", sceKernelLockLwMutex(&timed, 1, &timeout));
    test_join_thread(test_start_thread(short_waiter));
    PRINT("the waiter's lock with a 200 ms timeout", waiter_result);
    PRINT("delete the second mutex while the main thread holds it", sceKernelDeleteLwMutex(&timed));
    PRINT("unlock 1 after the delete", sceKernelUnlockLwMutex(&timed, 1));
    PRINTF(word_of(&timed, 0) == main_uid, "word 0 after that unlock is the main thread");
    PRINTF(word_of(&timed, 1), "word 1 after that unlock");
    PRINTF(word_of(&timed, 2), "word 2 after that unlock");
    PRINT("try lock 1 by the main thread, non recursive", sceKernelTryLockLwMutex(&timed, 1));

    sceKernelDeleteSema(gate);
    test_exit();
    return 0;
}
