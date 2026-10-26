// A recursive mutex that its owner unlocks one count at a time, with a thread waiting to lock it.
// The main thread locks the mutex to a count of 3, a thread W waits to lock it for a count of 1
// with a 2 s timeout, and the main thread unlocks it three times. After each of the first two
// unlocks, which leave the main thread owning the mutex, the test prints the unlock's result, W's
// wait type, the waiting thread count, the current count and whether the main thread still owns it.
// The third unlock takes the count to 0, and W gets the mutex and holds it until the test lets it
// go. The test prints the owner, the count and the main thread's try lock while W holds it, then
// W's result and its unlock. It is like the mutex case of handoff_more, which has a single unlock
// to 0. Every thread is pinned to core 0 and W runs below the main thread.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0

static SceUID mutex, gate;
static volatile int waiter_result = 0xDEAD, waiter_unlock = 0xDEAD;

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

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

static MutexInfo mutex_info(void) {
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    const SceUID main_thread = sceKernelGetThreadId();
    mutex = sceKernelCreateMutex("mutex", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    SceUInt32 lock_timeout = TEST_WAIT_US;
    PRINT("lock to a count of 3", sceKernelLockMutex(mutex, 3, &lock_timeout));

    const SceUID w =
        sceKernelCreateThread("waiter", waiter, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(wait_type_of(w) == WAIT_TYPE_MUTEX);

    for (int i = 1; i <= 2; i++) {
        const int unlock = sceKernelUnlockMutex(mutex, 1);
        const MutexInfo info = mutex_info();
        PRINTF(unlock, "partial unlock %d: the unlock", i);
        PRINTF(wait_type_of(w), "partial unlock %d: W's wait type", i);
        PRINTF(info.numWaitThreads, "partial unlock %d: the waiting threads", i);
        PRINTF(info.currentCount, "partial unlock %d: the count", i);
        PRINTF(info.currentOwnerId == main_thread, "partial unlock %d: the main thread still owns it", i);
    }

    PRINT("last unlock: the unlock", sceKernelUnlockMutex(mutex, 1));
    // W holds the mutex at the gate, so the owner and the count are read while it is blocked
    TEST_WAIT_FOR(wait_type_of(w) == WAIT_TYPE_SEMA);
    const MutexInfo held = mutex_info();
    PRINT("last unlock: W owns the mutex", held.currentOwnerId == w);
    PRINT("last unlock: the count", held.currentCount);
    PRINT("last unlock: the main thread's try lock", sceKernelTryLockMutex(mutex, 1));

    sceKernelSignalSema(gate, 1);
    test_join_thread(w);
    PRINT("W's result", waiter_result);
    PRINT("W's unlock", waiter_unlock);

    sceKernelDeleteMutex(mutex);
    sceKernelDeleteSema(gate);
    test_exit();
    return 0;
}
