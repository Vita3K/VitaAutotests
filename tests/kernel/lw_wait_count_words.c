// What the work area of a lightweight mutex holds while a thread waits for it, and after the
// handoff. The main thread locks the mutex, a thread W waits to lock it with a 2 s timeout, and the
// test prints the words of the work area while W waits: word 0 compared with the main thread's UID,
// then word 1 and word 2 as they are. The main thread unlocks, W gets the mutex and holds it at a
// gate, and the test prints the words again, word 0 compared with W's UID, then W is let go and
// unlocks. In round two the main thread locks the mutex again, a thread W2 waits with a 200 ms
// timeout and gives up, and the test prints word 1 after W2 is gone. Then the main thread unlocks
// and the test prints word 1 and the unlock's result. The model lw_work_layout prints the words of
// the work area without a waiter.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceKernelLwMutexWork mutex;
static SceUID gate;
static volatile int waiter_result = 0xDEAD, waiter_unlock = 0xDEAD;

// Waits for the mutex, then holds it at the gate and unlocks it
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    waiter_result = sceKernelLockLwMutex(&mutex, 1, &timeout);
    if (waiter_result == 0) {
        timeout = TEST_WAIT_US;
        sceKernelWaitSema(gate, 1, &timeout);
        waiter_unlock = sceKernelUnlockLwMutex(&mutex, 1);
    }
    return 0;
}

// Waits for the mutex for 200 ms
static int short_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 200000;
    waiter_result = sceKernelLockLwMutex(&mutex, 1, &timeout);
    return 0;
}

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

static unsigned int word(int i) { return ((const volatile unsigned int *)&mutex)[i]; }

static void print_words(const char *step, SceUID owner) {
    PRINTF(word(0) == (unsigned int)owner, "%s: word 0 is the owner's UID", step);
    PRINTF(word(1), "%s: word 1", step);
    PRINTF(word(2), "%s: word 2", step);
}

int main(void) {
    const SceUID main_thread = sceKernelGetThreadId();
    sceKernelCreateLwMutex(&mutex, "lw", 0, 0, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);

    // Round one: the main thread holds the mutex while W waits for it
    SceUInt32 lock_timeout = TEST_WAIT_US;
    PRINT("lock", sceKernelLockLwMutex(&mutex, 1, &lock_timeout));
    const SceUID w = test_start_thread(waiter);
    TEST_WAIT_FOR(wait_type_of(w) == WAIT_TYPE_LW_MUTEX);
    print_words("W waits", main_thread);

    PRINT("the main thread's unlock", sceKernelUnlockLwMutex(&mutex, 1));
    // W holds the mutex at the gate, so its words are read while it is blocked
    TEST_WAIT_FOR(wait_type_of(w) == WAIT_TYPE_SEMA);
    print_words("W holds it", w);
    sceKernelSignalSema(gate, 1);
    test_join_thread(w);
    PRINT("W's result", waiter_result);
    PRINT("W's unlock", waiter_unlock);

    // Round two: the main thread holds the mutex while W2 waits with a short timeout
    lock_timeout = TEST_WAIT_US;
    PRINT("lock again", sceKernelLockLwMutex(&mutex, 1, &lock_timeout));
    const SceUID w2 = test_start_thread(short_waiter);
    test_join_thread(w2);
    PRINT("W2's wait result", waiter_result);
    PRINT("word 1 after W2 gave up", word(1));
    PRINT("the main thread's unlock", sceKernelUnlockLwMutex(&mutex, 1));
    PRINT("word 1 after the unlock", word(1));

    sceKernelDeleteLwMutex(&mutex);
    sceKernelDeleteSema(gate);
    test_exit();
    return 0;
}
