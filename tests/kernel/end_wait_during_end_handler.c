// Thread end waits while the END thread event handler of the thread is still running. T at 0x70 on
// core 1 has an END handler, which the main thread registers with the END mask alone. T waits on a
// semaphore, then returns 0x66, so W1 is queued for T's end before T returns. The handler records
// that it runs, then spins on a flag until main sets it, or until a bound in process time. Each pass
// reads the process time, which doesn't block, and the handler makes no exit or delete call. While
// it spins, main reads W1's wait type and UID, T's status and its exit status.
// It starts W2 and then W3 with a 300 ms timeout, and W3 times out during the spin. W1 and W2 have
// long timeouts, so they stay queued until main sets the flag, however long the spin lasts. Cases:
// - W3's wait for T's end times out during the handler, and W2 still waits for T's end
// - a wait for T's end by main with timeout 0 times out during the handler
// - after main sets the flag, the handler returns and T ends with 0x66: W1 and W2 get 0 with
//   status 0x66, and a new wait for T's end returns at once with the same status
// The handler also records whether its spin ended by the flag, rather than by its bound.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define TARGET_PRIORITY 0x70
#define WAITER_PRIORITY 0x80
#define WAITERS 3
#define W1 0
#define W2 1
#define W3 2
// Bounds the spin in process time, so a run that never sets the flag still ends
#define SPIN_LIMIT_US 10000000

static SceUID go, target, handler_id;
static volatile int handler_in, handler_go, handler_saw_flag;
static volatile int waiter_done[WAITERS], waiter_result[WAITERS], waiter_status[WAITERS];
static const SceUInt waiter_timeout[WAITERS] = { 10 * TEST_WAIT_US, 10 * TEST_WAIT_US, 300000 };

// The END handler of T. It spins until main sets handler_go. Each pass reads the process time, a
// call that doesn't block, so the other threads of the test get to run while it spins.
static int end_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    handler_in = 1;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    while (!handler_go && sceKernelGetProcessTimeWide() - start < SPIN_LIMIT_US)
        ;
    handler_saw_flag = handler_go;
    return 0;
}

// Waits on the semaphore, then returns 0x66, so the END handler runs once T has returned
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(go, 1, &timeout);
    return 0x66;
}

// Waits for T's end with the timeout of its index, and records its result and status
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    int status = 0xDEAD;
    SceUInt timeout = waiter_timeout[i];
    waiter_result[i] = sceKernelWaitThreadEnd(target, &status, &timeout);
    waiter_status[i] = status;
    waiter_done[i] = 1;
    return 0;
}

// Starts waiter i on core 0, where it waits for T's end as soon as it runs
static SceUID start_waiter(int i) {
    const SceUID thread =
        sceKernelCreateThread("waiter", waiter_entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, sizeof(i), &i);
    return thread;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);

    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    target = sceKernelCreateThread("target", target_entry, TARGET_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    handler_id = sceKernelRegisterThreadEventHandler("end", target, SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, end_handler, NULL);
    PRINT("register the END handler", test_created(handler_id));
    sceKernelStartThread(target, 0, NULL);

    const SceUID w1 = start_waiter(W1);
    TEST_WAIT_FOR(test_in_wait(w1, WAIT_TYPE_THREAD_END, target));
    sceKernelSignalSema(go, 1);
    TEST_WAIT_FOR(handler_in);

    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(w1, &info);
    PRINT("W1's wait type while T is in its handler", info.waitType);
    PRINT("W1's wait UID is T", info.waitId == target);
    sceKernelGetThreadInfo(target, &info);
    PRINT("T's status in its handler", info.status);
    int status = 0xDEAD;
    PRINT("T's exit status in its handler", sceKernelGetThreadExitStatus(target, &status));
    PRINT("the exit status written", status);

    const SceUID w2 = start_waiter(W2);
    TEST_WAIT_FOR(test_in_wait(w2, WAIT_TYPE_THREAD_END, target));
    const SceUID w3 = start_waiter(W3);
    TEST_WAIT_FOR(waiter_done[W3]);
    test_join_thread(w3);
    PRINT("W3's wait for T's end", waiter_result[W3]);
    PRINT("W2 still waits for T's end", test_in_wait(w2, WAIT_TYPE_THREAD_END, target));
    SceUInt zero = 0;
    PRINT("a wait for T's end by main with timeout 0", sceKernelWaitThreadEnd(target, NULL, &zero));

    // Lets the handler return, so T ends and its waiters get its status
    handler_go = 1;
    test_join_thread(w1);
    test_join_thread(w2);
    PRINT("W1's result after T returned", waiter_result[W1]);
    PRINT("W1's status", waiter_status[W1]);
    PRINT("W2's result", waiter_result[W2]);
    PRINT("W2's status", waiter_status[W2]);
    PRINT("the handler's spin ended by the flag", handler_saw_flag);
    SceUInt new_timeout = 0;
    status = 0xDEAD;
    PRINT("a new wait for T's end", sceKernelWaitThreadEnd(target, &status, &new_timeout));
    PRINT("the status of the new wait", status);
    PRINTF(sceKernelUnregisterThreadEventHandler(handler_id), "unregister the handler");
    sceKernelDeleteThread(target);
    sceKernelDeleteSema(go);

    test_exit();
    return 0;
}
