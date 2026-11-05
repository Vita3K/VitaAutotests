// A ForVM suspend of a thread while it runs its END thread event handler, which the main thread
// registers with the END mask alone. The target T has the VM attribute and runs on core 0, the main
// thread runs on core 1. T locks a mutex M, then returns 0x77, so the handler runs with M still
// held. Cases:
// - the handler spins in user code, with no call, counting until a gate opens. Main suspends T,
//   then prints the result, T's status, its exit status, whether T still owns M, a try lock of M,
//   and a wait for T's end with a 10 ms timeout. It checks that the count didn't change over the
//   next 20 ms. It resumes T and opens the gate, then prints the wait for T's end with the status
//   it writes, and a try lock of M.
// - the handler waits on a semaphore. Main suspends T while it waits, signals the semaphore, and
//   waits until T no longer waits. It prints T's status and wait type, whether the handler went on
//   after the wait, and after a resume the wait for T's end and the handler's wait result.
// Every spin and wait is bounded, so a run that never stops or releases T still ends.
//
// On a PS Vita TV on firmware 3.65 the suspend returns 1 in both cases. In the spin case T shows
// SUSPENDED 0x100 in its handler. It still owns M, so a try lock of M fails with 0x80028145. The
// exit status call fails with 0x80028028 and writes nothing, the wait for T's end with 10 ms times
// out with 0x80028005, and the count doesn't change. After the resume and the gate the wait for T's
// end gives 0 with the exit status 0x77, and M can be locked. In the wait case T still shows
// WAITING 0x8 with the semaphore wait type 0x20 right after the suspend. After the signal T shows
// SUSPENDED 0x100 with wait type 0, and the handler has not gone on after its wait. After the
// resume the handler goes on, its wait result is 0, and T ends with the exit status 0x77.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// Bounds the spin in loops, so a run that never opens the gate still ends
#define SPINS 400000000

enum Mode { SPIN, WAIT };

static SceUID mutex, sema, target;
static volatile int mode, gate, handler_wait_result, handler_went_on;
static volatile unsigned int counter;

// The END handler of T. It spins or waits on the semaphore, by the mode of the case.
static int end_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    if (mode == SPIN) {
        for (int n = 0; n < SPINS && !gate; n++)
            counter++;
    } else {
        SceUInt timeout = 5 * TEST_WAIT_US;
        handler_wait_result = sceKernelWaitSema(sema, 1, &timeout);
        handler_went_on = 1;
    }
    return 0;
}

// Locks the mutex and returns, so the END handler runs with the mutex held
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    return 0x77;
}

static int status_of(SceUID thread, SceUInt32 *wait_type) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    if (sceKernelGetThreadInfo(thread, &info) < 0)
        return -1;
    if (wait_type)
        *wait_type = info.waitType;
    return info.status;
}

static int mutex_owner_is_target(void) {
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info.currentOwnerId == target;
}

// Creates the mutex, the semaphore and T with its handler, and starts T
static SceUID start_case(int new_mode) {
    mode = new_mode;
    gate = handler_wait_result = handler_went_on = 0;
    counter = 0;
    mutex = sceKernelCreateMutex("M", 0, 0, NULL);
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID handler = sceKernelRegisterThreadEventHandler("end", target, SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, end_handler, NULL);
    PRINT("register the END handler", test_created(handler));
    sceKernelStartThread(target, 0, NULL);
    return handler;
}

static void end_case(SceUID handler) {
    PRINT("unregister the handler", sceKernelUnregisterThreadEventHandler(handler));
    PRINT("delete T", sceKernelDeleteThread(target));
    sceKernelDeleteMutex(mutex);
    sceKernelDeleteSema(sema);
}

static void case_spin(void) {
    const SceUID handler = start_case(SPIN);
    TEST_WAIT_FOR(counter > 0);

    const int suspended = sceKernelSuspendThreadForVM(target);
    const unsigned int first = counter;
    const int status = status_of(target, NULL);
    int exit_status = 0xDEAD;
    const int exit_result = sceKernelGetThreadExitStatus(target, &exit_status);
    const int owner = mutex_owner_is_target();
    const int try_lock = sceKernelTryLockMutex(mutex, 1);
    SceUInt timeout = 10000;
    const int wait = sceKernelWaitThreadEnd(target, NULL, &timeout);
    sceKernelDelayThread(20000);
    const unsigned int second = counter;
    PRINT("spin: suspend T in its END handler", suspended);
    PRINT("spin: T's status", status);
    PRINT("spin: T's exit status call", exit_result);
    PRINT("spin: the exit status written", exit_status);
    PRINT("spin: T still owns M", owner);
    PRINT("spin: try lock M", try_lock);
    PRINT("spin: wait for T's end with 10 ms", wait);
    PRINT("spin: the count stays as it was right after the suspend", second == first);

    PRINT("spin: resume T", sceKernelResumeThreadForVM(target));
    gate = 1;
    int end_status = 0xDEAD;
    timeout = 5 * TEST_WAIT_US;
    PRINT("spin: wait for T's end", sceKernelWaitThreadEnd(target, &end_status, &timeout));
    PRINT("spin: T's exit status", end_status);
    const int try_after = sceKernelTryLockMutex(mutex, 1);
    PRINT("spin: try lock M after the end", try_after);
    if (try_after == 0)
        sceKernelUnlockMutex(mutex, 1);
    end_case(handler);
}

static void case_wait(void) {
    const SceUID handler = start_case(WAIT);
    TEST_WAIT_FOR(test_in_wait(target, WAIT_TYPE_SEMA, sema));

    PRINT("wait: suspend T while its handler waits", sceKernelSuspendThreadForVM(target));
    SceUInt32 wait_type = 0xDEAD;
    PRINT("wait: T's status", status_of(target, &wait_type));
    PRINT("wait: T's wait type", wait_type);
    PRINT("wait: signal the semaphore", sceKernelSignalSema(sema, 1));
    // The status leaves WAITING when the signal wakes T, and reads 0x100 once T stopped
    TEST_WAIT_FOR(status_of(target, NULL) != 0x8 && (status_of(target, NULL) == 0x100 || handler_went_on));
    sceKernelDelayThread(20000);
    const int status = status_of(target, &wait_type);
    const int went_on = handler_went_on;
    PRINT("wait: T's status after the signal", status);
    PRINT("wait: T's wait type after the signal", wait_type);
    PRINT("wait: the handler went on after its wait", went_on);

    PRINT("wait: resume T", sceKernelResumeThreadForVM(target));
    int end_status = 0xDEAD;
    SceUInt timeout = 5 * TEST_WAIT_US;
    PRINT("wait: wait for T's end", sceKernelWaitThreadEnd(target, &end_status, &timeout));
    PRINT("wait: T's exit status", end_status);
    PRINT("wait: the handler's wait result", handler_wait_result);
    PRINT("wait: the handler went on after the resume", handler_went_on);
    end_case(handler);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    case_spin();
    case_wait();

    test_exit();
    return 0;
}
