// What the priority of a thread is while it waits on a condition variable, while it waits to get
// its mutex back after a signal, and after it has the mutex back, for a mutex with the ceiling
// 0x80. Owner O, at 0xB0 on core 0, locks the mutex M, waits on the condition C with a 2 s timeout,
// and once the wait returns it holds M and waits at a gate before it unlocks. The main thread, on
// core 1 at 0xA0, reads O's priority while O waits on C, locks M, signals C, and reads O's priority
// and wait type while O waits for M. It then unlocks M and reads O's priority once O is at the
// gate. The main thread prints these readings, and O's wait result, once O has ended. The wait type
// is printed as the thread info reports it, with the callback bit if any.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define OWNER_PRIORITY 0xB0
#define CEILING 0x80

static SceUID mutex, cond, gate;
static volatile int owner_wait;

// The thread info of a thread, or zeros if it is unknown
static SceKernelThreadInfo thread_info(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info;
}

// The type of the wait the thread is in, without the callback bit, or 0 if it doesn't wait
static SceUInt32 wait_type(SceUID thread) {
    SceKernelThreadInfo info = thread_info(thread);
    if (!(info.status & SCE_THREAD_WAITING))
        return 0;
    return info.waitType & ~WAIT_TYPE_CB;
}

// Locks M, waits on C, then waits at the gate with M held and unlocks it
static int owner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    SceUInt32 timeout = 2 * TEST_WAIT_US;
    owner_wait = sceKernelWaitCond(cond, &timeout);
    timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    sceKernelUnlockMutex(mutex, 1);
    return 0;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = CEILING };
    mutex = sceKernelCreateMutex("ceiling", SCE_KERNEL_MUTEX_ATTR_CEILING, 0, &opt);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    owner_wait = -1;

    const SceUID owner = sceKernelCreateThread("owner", owner_entry, OWNER_PRIORITY, 0x4000, 0,
                                               SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(owner, 0, NULL);
    // O's wait on the condition releases the mutex, so it is free for the main thread to lock
    TEST_WAIT_FOR(wait_type(owner) == WAIT_TYPE_COND);
    const int waiting_on_cond = thread_info(owner).currentPriority;

    SceUInt32 timeout = 2 * TEST_WAIT_US;
    const int lock = sceKernelLockMutex(mutex, 1, &timeout);
    const int signal = sceKernelSignalCond(cond);
    TEST_WAIT_FOR(wait_type(owner) == WAIT_TYPE_COND_MUTEX);
    const SceKernelThreadInfo waiting_info = thread_info(owner);
    const int unlock = sceKernelUnlockMutex(mutex, 1);
    // O holds the mutex again once it waits at the gate, after its wait returned
    TEST_WAIT_FOR(test_in_wait(owner, WAIT_TYPE_SEMA, gate));
    const int regained = thread_info(owner).currentPriority;

    sceKernelSignalSema(gate, 1);
    test_join_thread(owner);

    PRINTF(waiting_on_cond, "O's priority while it waits on the condition");
    PRINTF(lock, "the main thread's lock of the mutex");
    PRINTF(signal, "the signal");
    PRINTF(waiting_info.currentPriority, "O's priority while it waits for the mutex");
    PRINTF(waiting_info.waitType, "O's wait type while it waits for the mutex");
    PRINTF(unlock, "the main thread's unlock");
    PRINTF(regained, "O's priority once it has the mutex back");
    PRINTF(owner_wait, "O's wait result");

    sceKernelDeleteSema(gate);
    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(mutex);

    test_exit();
    return 0;
}
