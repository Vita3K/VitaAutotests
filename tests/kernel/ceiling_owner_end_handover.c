// What a thread that gets a ceiling mutex from an owner that ends has for its priority, its base
// priority and its count, and what its priority is after it unlocks. Owner O, at 0xB0 on core 0,
// locks a mutex with the ceiling 0x80 and ends once the main thread signals it, without unlocking.
// Waiter W, at 0x90 on core 0, waits for the mutex, so the owner's end hands it to W. W then holds
// at a gate until the main thread lets it unlock. The main thread, on core 1, waits for W to be at
// the gate, then prints W's current and initial priorities from its info, whether W owns the
// mutex and its count, and lets W unlock. W prints its priority after the unlock. Lines for the
// results of W's lock and unlock are printed too.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define OWNER_PRIORITY 0xB0
#define WAITER_PRIORITY 0x90
#define CEILING 0x80

static SceUID mutex, go, gate;
static volatile int waiter_lock, waiter_unlock, waiter_after;

// Locks the mutex, waits for go and ends without giving the mutex back
static int owner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    SceUInt32 timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(go, 1, &timeout);
    return 0;
}

// Locks the mutex, waits at the gate, then unlocks it and notes its priority
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 2 * TEST_WAIT_US;
    waiter_lock = sceKernelLockMutex(mutex, 1, &timeout);
    if (waiter_lock == 0) {
        timeout = 2 * TEST_WAIT_US;
        sceKernelWaitSema(gate, 1, &timeout);
        waiter_unlock = sceKernelUnlockMutex(mutex, 1);
        waiter_after = sceKernelGetThreadCurrentPriority();
    }
    return 0;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = CEILING };
    mutex = sceKernelCreateMutex("ceiling", SCE_KERNEL_MUTEX_ATTR_CEILING, 0, &opt);
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    waiter_lock = waiter_unlock = waiter_after = -1;

    const SceUID owner = sceKernelCreateThread("owner", owner_entry, OWNER_PRIORITY, 0x4000, 0,
                                               SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID waiter = sceKernelCreateThread("waiter", waiter_entry, WAITER_PRIORITY, 0x4000, 0,
                                                SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(owner, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(owner, WAIT_TYPE_SEMA, go));
    sceKernelStartThread(waiter, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(waiter, WAIT_TYPE_MUTEX, mutex));

    sceKernelSignalSema(go, 1);
    test_join_thread(owner);
    // W holds the mutex once it waits at the gate, which only happens after its lock returned
    TEST_WAIT_FOR(test_in_wait(waiter, WAIT_TYPE_SEMA, gate));
    SceKernelThreadInfo thread_info = { .size = sizeof(thread_info) };
    sceKernelGetThreadInfo(waiter, &thread_info);
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    const int current = thread_info.currentPriority, initial = thread_info.initPriority;
    const int owns = info.currentOwnerId == waiter;
    const int count = info.currentCount;

    sceKernelSignalSema(gate, 1);
    test_join_thread(waiter);

    PRINTF(waiter_lock, "the waiter's lock");
    PRINTF(current, "the waiter's current priority once it owns the mutex");
    PRINTF(initial, "the waiter's initial priority");
    PRINTF(owns, "the waiter owns the mutex");
    PRINTF(count, "the count after the handover");
    PRINTF(waiter_unlock, "the waiter's unlock");
    PRINTF(waiter_after, "the waiter's priority after its unlock");

    sceKernelDeleteSema(gate);
    sceKernelDeleteSema(go);
    sceKernelDeleteMutex(mutex);

    test_exit();
    return 0;
}
