// The count of a recursive lightweight mutex that wraps around to zero for its owner. The owner
// locks the mutex to a count of 2, then 1, then 0x7FFFFFFF, which wraps the count to 0x80000002.
// Then it tries to lock with 0x7FFFFFFE, which adds up to 0 with the wrapped count, and locks with
// 0x7FFFFFFE with a timeout, so a wait can't hang the test. After each call the test prints the
// result and the count word of the work area, and word 0 compared with the owner's UID. Then it
// unlocks once, has another thread try to lock, and deletes the mutex. The test prints each result,
// the count word and the other thread's try lock. This test stands alone, since a wrong count may
// confuse the checks after it. The models are lw_mutex_user_paths, for the wrapped count, and
// lw_mutex.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceKernelLwMutexWork mutex;
static volatile int other_result = 0xDEAD;

// Tries to lock the mutex with a count of 1 and records the result
static int other_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    other_result = sceKernelTryLockLwMutex(&mutex, 1);
    return 0;
}

static unsigned int word(int i) { return ((const volatile unsigned int *)&mutex)[i]; }

static void print_words(const char *step, SceUID owner) {
    PRINTF(word(0) == (unsigned int)owner, "%s: word 0 is the owner's UID", step);
    PRINTF(word(2), "%s: word 2, the count", step);
}

int main(void) {
    const SceUID owner = sceKernelGetThreadId();
    sceKernelCreateLwMutex(&mutex, "zero", LW_MUTEX_ATTR_RECURSIVE, 0, NULL);

    // Every lock has a timeout, so a wrong implementation that waits can't hang the test
    SceUInt32 timeout = TEST_WAIT_US;
    PRINT("lock of 2", sceKernelLockLwMutex(&mutex, 2, &timeout));
    print_words("after the lock of 2", owner);
    timeout = TEST_WAIT_US;
    PRINT("lock of 1", sceKernelLockLwMutex(&mutex, 1, &timeout));
    print_words("after the lock of 1", owner);
    timeout = TEST_WAIT_US;
    PRINT("lock of 0x7FFFFFFF", sceKernelLockLwMutex(&mutex, 0x7FFFFFFF, &timeout));
    print_words("after the lock of 0x7FFFFFFF", owner);

    PRINT("try lock of 0x7FFFFFFE", sceKernelTryLockLwMutex(&mutex, 0x7FFFFFFE));
    print_words("after the try lock", owner);
    timeout = TEST_WAIT_US;
    PRINT("lock of 0x7FFFFFFE with a timeout", sceKernelLockLwMutex(&mutex, 0x7FFFFFFE, &timeout));
    print_words("after the lock", owner);

    PRINT("unlock of 1", sceKernelUnlockLwMutex(&mutex, 1));
    print_words("after the unlock", owner);
    test_join_thread(test_start_thread(other_thread));
    PRINT("another thread's try lock", other_result);

    PRINT("delete", sceKernelDeleteLwMutex(&mutex));
    test_exit();
    return 0;
}
