// A copy of the work area of a lightweight mutex, made in memory, and what the user calls and the
// kernel calls do with it. The user lock and unlock only read the words they are given, so a copy
// can lock by itself while its own words say the mutex is free. The test checks whether the kernel
// accepts a copy, and what info by work area reports for one. The cases:
// - a copy made while the original is free, locked and unlocked by the main thread through the
//   copy, and the original's words after the copy's lock
// - while the main thread holds the copy, another thread locks the original with a zero timeout
// - a second lock of the copy, non recursive, with a 2 s timeout, timed against 1 s
// - unlocks of the copy with a count of 0 and with a count of 1
// - info by work area of the free copy, compared with the original's work area, count and owner
// - a copy made while the other thread holds the original: a lock of the copy with a 2 s timeout,
//   timed against 1 s, and a try lock of the copy
// - a delete through that copy, which must not end the original, then the original is locked,
//   unlocked and deleted by the main thread
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceKernelLwMutexWork original;
static SceUID gate;
static volatile unsigned int holder_uid;
static volatile int holder_lock = 0xDEAD, holder_unlock = 0xDEAD;

// Locks the original with a zero timeout, then holds it until the gate is signaled
static int holder_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    holder_uid = sceKernelGetThreadId();
    SceUInt32 timeout = 0;
    holder_lock = sceKernelLockLwMutex(&original, 1, &timeout);
    timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    holder_unlock = sceKernelUnlockLwMutex(&original, 1);
    return 0;
}

// The word of a work area with the given index, as an unsigned value
static unsigned int word_of(const void *work, int i) {
    return ((const volatile unsigned int *)work)[i];
}

// Prints whether the owner, wait count and count words of work are all 0
static void print_free_words(const char *step, const void *work) {
    for (int i = 0; i < 3; i++)
        PRINTF(word_of(work, i) == 0, "%s: word %d is 0", step, i);
}

int main(void) {
    const unsigned int main_uid = (unsigned int)sceKernelGetThreadId();
    sceKernelCreateLwMutex(&original, "original", 0, 0, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);

    // A copy made while the original is free locks and unlocks by itself
    static SceKernelLwMutexWork copy;
    sceClibMemcpy(&copy, &original, sizeof(original));
    SceUInt32 timeout = TEST_WAIT_US;
    PRINT("lock the copy while the original is free", sceKernelLockLwMutex(&copy, 1, &timeout));
    PRINTF(word_of(&copy, 0) == main_uid, "the copy's word 0 is the caller");
    PRINTF(word_of(&copy, 2), "the copy's count after its lock");
    print_free_words("the original after the copy's lock", &original);

    // While the main thread holds the copy, another thread locks the original, which has its
    // own words
    const SceUID holder = test_start_thread(holder_entry);
    TEST_WAIT_FOR(test_in_wait(holder, WAIT_TYPE_SEMA, gate));
    PRINT("the other thread's lock of the original with a zero timeout", holder_lock);
    PRINTF(word_of(&original, 0) == holder_uid, "the original's word 0 is the other thread");
    PRINTF(word_of(&original, 2), "the original's count after the other thread's lock");
    PRINTF(word_of(&copy, 0) == main_uid, "the copy's word 0 is still the caller");

    // A second lock of the copy is not recursive, so it goes to the kernel. A wait that starts
    // shows in the time the call takes.
    timeout = TEST_WAIT_US;
    const SceUInt64 relock_start = sceKernelGetProcessTimeWide();
    const int relock = sceKernelLockLwMutex(&copy, 1, &timeout);
    const SceUInt64 relock_elapsed = sceKernelGetProcessTimeWide() - relock_start;
    PRINT("lock the copy again, non recursive, with a 2 s timeout", relock);
    PRINTF(relock_elapsed < 1000000, "lock the copy again returned in under 1 s");
    PRINTF(word_of(&original, 0) == holder_uid, "the original's owner after that lock");

    // Unlocks of the copy with a count of 0 go to the kernel, and with a count of 1 the copy's
    // own words say it is owned by the caller
    PRINT("unlock the copy with a count of 0", sceKernelUnlockLwMutex(&copy, 0));
    PRINT("unlock the copy with a count of 1", sceKernelUnlockLwMutex(&copy, 1));
    print_free_words("the copy after its unlock", &copy);
    PRINTF(word_of(&original, 0) == holder_uid, "the original's owner after the copy's unlock");
    PRINTF(word_of(&original, 2), "the original's count after the copy's unlock");

    // Info by work area of the free copy reports the original's state, not the copy's
    LwMutexInfo info = { .size = sizeof(info) };
    PRINT("info by work area of the copy", sceKernelGetLwMutexInfo(&copy, &info));
    PRINTF(info.work == &original, "info's work area is the original's");
    PRINTF(info.currentCount == (SceInt32)word_of(&original, 2), "info's count is the original's");
    PRINTF(info.currentOwnerId == (SceUID)word_of(&original, 0), "info's owner is the original's");

    // A copy made while the other thread holds the original is refused by the kernel as well
    static SceKernelLwMutexWork held_copy;
    sceClibMemcpy(&held_copy, &original, sizeof(original));
    timeout = TEST_WAIT_US;
    const SceUInt64 held_start = sceKernelGetProcessTimeWide();
    const int held_lock = sceKernelLockLwMutex(&held_copy, 1, &timeout);
    const SceUInt64 held_elapsed = sceKernelGetProcessTimeWide() - held_start;
    PRINT("lock the copy made while the other thread holds the original", held_lock);
    PRINTF(held_elapsed < 1000000, "that lock returned in under 1 s");
    PRINT("try lock of that copy", sceKernelTryLockLwMutex(&held_copy, 1));

    // Deleting through the copy must not end the original, which the other thread still holds
    PRINT("delete through the copy made while it is held", sceKernelDeleteLwMutex(&held_copy));
    sceKernelSignalSema(gate, 1);
    test_join_thread(holder);
    PRINT("the other thread's unlock of the original", holder_unlock);

    timeout = TEST_WAIT_US;
    PRINT("lock the original after the delete through the copy",
          sceKernelLockLwMutex(&original, 1, &timeout));
    PRINT("unlock the original", sceKernelUnlockLwMutex(&original, 1));
    PRINT("delete the original", sceKernelDeleteLwMutex(&original));

    sceKernelDeleteSema(gate);
    test_exit();
    return 0;
}
