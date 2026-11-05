// What a thread still holds while its END thread event handler runs. T at 0x70 on core 1 has an END
// handler, which the main thread registers with the END mask alone. T waits on an event flag
// without SCE_EVENT_WAITMULTIPLE for a bit that is never set, with a timeout of 0, so the wait
// times out at once. The poll of the flag shows whether T still counts as a waiting thread of it.
// Then T locks a lightweight mutex, a mutex and the write lock of a rwlock, waits on a semaphore,
// and returns. The handler records that it runs, then spins until the main thread sets a flag, or
// until a bound in process time. Each pass reads the process time, which doesn't block, and the
// handler makes no exit or delete call. The main thread, on core 2, tries each object while T waits
// on the semaphore, while T is in the handler and after T ended. Every try that succeeds is undone,
// and every result is printed. The lightweight mutex's owner word is compared with T's UID, and the
// flag is tried with a poll for a bit that is set, so a poll that gets no error succeeds with 0.
//
// On a PS Vita TV on firmware 3.65 the handler runs and T ends with the exit status 0x55. While T
// waits on the semaphore and while it is in its END handler, every try fails. The lightweight mutex
// gives 0x80028185, the mutex 0x80028145, both rwlock locks 0x800281E6, and the poll of the flag
// 0x800280E2. After T ended the mutex, both rwlock locks and the poll of the flag succeed with 0.
// The lightweight mutex stays locked, its owner word is still T's UID and a try lock fails with
// 0x80028185. The owner word is T's UID in all three steps.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define TARGET_PRIORITY 0x70
// Bounds the spin in process time, so a run that never sets the flag still ends
#define SPIN_LIMIT_US 10000000

static SceUID flag, mutex, rwlock, go, target;
static SceKernelLwMutexWork lw_mutex;
static volatile int handler_in, handler_go, setup_done;

// The END handler of T. It spins until main sets handler_go.
static int end_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    handler_in = 1;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    while (!handler_go && sceKernelGetProcessTimeWide() - start < SPIN_LIMIT_US)
        ;
    return 0;
}

// Takes the holds, waits on the semaphore, then returns without giving anything back
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 bits;
    SceUInt timeout = 0;
    sceKernelWaitEventFlag(flag, 0x1, SCE_EVENT_WAITOR, &bits, &timeout);
    SceUInt32 lock_timeout = 2 * TEST_WAIT_US;
    sceKernelLockLwMutex(&lw_mutex, 1, &lock_timeout);
    lock_timeout = 2 * TEST_WAIT_US;
    sceKernelLockMutex(mutex, 1, &lock_timeout);
    lock_timeout = 2 * TEST_WAIT_US;
    sceKernelLockWriteRWLock(rwlock, &lock_timeout);
    setup_done = 1;
    timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(go, 1, &timeout);
    return 0x55;
}

// Tries each object and prints the results under the given step
static void try_objects(const char *step) {
    PRINTF(((const volatile unsigned int *)&lw_mutex)[0] == (unsigned int)target, "%s: the lightweight mutex's owner word is T", step);
    int result = sceKernelTryLockLwMutex(&lw_mutex, 1);
    PRINTF(result, "%s: try lock of the lightweight mutex", step);
    if (result == 0)
        sceKernelUnlockLwMutex(&lw_mutex, 1);
    result = sceKernelTryLockMutex(mutex, 1);
    PRINTF(result, "%s: try lock of the mutex", step);
    if (result == 0)
        sceKernelUnlockMutex(mutex, 1);
    result = sceKernelTryLockReadRWLock(rwlock);
    PRINTF(result, "%s: try read lock of the rwlock", step);
    if (result == 0)
        sceKernelUnlockReadRWLock(rwlock);
    result = sceKernelTryLockWriteRWLock(rwlock);
    PRINTF(result, "%s: try write lock of the rwlock", step);
    if (result == 0)
        sceKernelUnlockWriteRWLock(rwlock);
    SceUInt32 bits = 0;
    PRINTF(sceKernelPollEventFlag(flag, 0x2, SCE_EVENT_WAITOR, &bits), "%s: poll of the event flag", step);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);

    flag = sceKernelCreateEventFlag("flag", 0, 0x2, NULL);
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 0, NULL);

    target = sceKernelCreateThread("target", target_entry, TARGET_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    const SceUID handler_id = sceKernelRegisterThreadEventHandler("end", target, SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, end_handler, NULL);
    PRINT("register the END handler", test_created(handler_id));
    sceKernelStartThread(target, 0, NULL);

    TEST_WAIT_FOR(setup_done && test_in_wait(target, WAIT_TYPE_SEMA, go));
    try_objects("T waits on the semaphore");

    sceKernelSignalSema(go, 1);
    TEST_WAIT_FOR(handler_in);
    PRINT("the handler runs", handler_in);
    try_objects("T is in its END handler");

    // Lets the handler return, so T ends
    handler_go = 1;
    int status = 0xDEAD;
    SceUInt timeout = 2 * TEST_WAIT_US;
    PRINT("the wait for T's end", sceKernelWaitThreadEnd(target, &status, &timeout));
    PRINT("T's exit status", status);
    try_objects("T ended");

    PRINTF(sceKernelUnregisterThreadEventHandler(handler_id), "unregister the handler");
    sceKernelDeleteThread(target);
    sceKernelDeleteLwMutex(&lw_mutex);
    sceKernelDeleteRWLock(rwlock);
    sceKernelDeleteMutex(mutex);
    sceKernelDeleteEventFlag(flag);
    sceKernelDeleteSema(go);

    test_exit();
    return 0;
}
