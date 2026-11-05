// What a thread suspended with sceKernelSuspendThreadForVM reports right after the wake that
// ends its wait, before it can run again. W has the VM attribute and runs on the main thread's
// core at a lower priority, so W only runs once the main thread waits. W waits with no timeout on
// a semaphore, and in a second case on a condition variable whose mutex W holds and the signal
// leaves free. The main thread waits until W reports the wait, suspends W, wakes it, and in the
// next calls reads W's status and wait type and the object. A later read of W's status and wait
// type comes after the main thread polls until W is neither RUNNING nor READY and waits for
// nothing, for at most 200 ms. Then the main thread resumes W and joins it.
//
// On a PS Vita TV on firmware 3.65 W shows READY 0x2 with wait type 0 right after the wake in
// both cases, and SUSPENDED 0x100 with wait type 0 once it has settled. The semaphore has count 0
// and no waiter right after the signal, so W has already taken the count. The mutex has no
// waiter and W does not own it right after the signal, and W owns it once it has settled. W's
// result is 0 in both cases.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x80
#define WAITER_PRIORITY 0xA0

static SceUID sema, mutex, cond;

static int sema_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return sceKernelWaitSema(sema, 1, NULL);
}

static int cond_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    const int result = sceKernelWaitCond(cond, NULL);
    sceKernelUnlockMutex(mutex, 1);
    return result;
}

// Whether thread is in a wait of the given type
static int waits_in(SceUID thread, SceUInt32 type) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType == type;
}

// Polls until the thread is neither RUNNING nor READY and waits for nothing
static void wait_until_settled(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    for (int i = 0; i < 200; i++) {
        sceKernelGetThreadInfo(thread, &info);
        if (info.waitType == 0 && !(info.status & (SCE_THREAD_RUNNING | SCE_THREAD_READY)))
            return;
        sceKernelDelayThread(1000);
    }
}

// Prints W's status and wait type under the label
static void print_state(const char *name, const char *when, SceUID w) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(w, &info);
    PRINTF(info.status, "%s: W's status %s", name, when);
    PRINTF(info.waitType, "%s: W's wait type %s", name, when);
}

static void run_sema(void) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID w = sceKernelCreateThread(
        "w", sema_waiter, WAITER_PRIORITY, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(waits_in(w, WAIT_TYPE_SEMA));
    PRINTF(sceKernelSuspendThreadForVM(w), "semaphore: suspend W");
    const int signaled = sceKernelSignalSema(sema, 1);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    const int got_info = sceKernelGetThreadInfo(w, &info);
    SceKernelSemaInfo sema_info = { .size = sizeof(sema_info) };
    const int got_sema = sceKernelGetSemaInfo(sema, &sema_info);
    PRINTF(signaled, "semaphore: the signal");
    PRINTF(got_info, "semaphore: get W's info at once");
    PRINTF(info.status, "semaphore: W's status at once");
    PRINTF(info.waitType, "semaphore: W's wait type at once");
    PRINTF(got_sema, "semaphore: get the semaphore's info");
    PRINTF(sema_info.currentCount, "semaphore: the count");
    PRINTF(sema_info.numWaitThreads, "semaphore: the waiters");
    wait_until_settled(w);
    print_state("semaphore", "later", w);
    PRINTF(sceKernelResumeThreadForVM(w), "semaphore: resume W");
    SceInt32 exit_status = 0xDEAD;
    sceKernelWaitThreadEnd(w, &exit_status, NULL);
    PRINTF(exit_status, "semaphore: W's result");
    sceKernelDeleteThread(w);
    sceKernelDeleteSema(sema);
}

static void run_cond(void) {
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    const SceUID w = sceKernelCreateThread(
        "w", cond_waiter, WAITER_PRIORITY, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(waits_in(w, WAIT_TYPE_COND));
    PRINTF(sceKernelSuspendThreadForVM(w), "condition: suspend W");
    const int signaled = sceKernelSignalCond(cond);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    const int got_info = sceKernelGetThreadInfo(w, &info);
    MutexInfo mutex_info = { .size = sizeof(mutex_info) };
    const int got_mutex = sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&mutex_info);
    PRINTF(signaled, "condition: the signal");
    PRINTF(got_info, "condition: get W's info at once");
    PRINTF(info.status, "condition: W's status at once");
    PRINTF(info.waitType, "condition: W's wait type at once");
    PRINTF(got_mutex, "condition: get the mutex's info");
    PRINTF(mutex_info.currentOwnerId == w, "condition: W owns the mutex at once");
    PRINTF(mutex_info.numWaitThreads, "condition: the mutex's waiters");
    wait_until_settled(w);
    print_state("condition", "later", w);
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&mutex_info);
    PRINTF(mutex_info.currentOwnerId == w, "condition: W owns the mutex later");
    PRINTF(sceKernelResumeThreadForVM(w), "condition: resume W");
    SceInt32 exit_status = 0xDEAD;
    sceKernelWaitThreadEnd(w, &exit_status, NULL);
    PRINTF(exit_status, "condition: W's result");
    sceKernelDeleteThread(w);
    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(mutex);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    run_sema();
    run_cond();

    test_exit();
    return 0;
}
