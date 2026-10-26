// What a semaphore signal does when it is made by the END thread event handler of a thread, and the
// waiter it wakes is on the same core. T at 0x70 on core 0 has an END handler, registered by the
// main thread with the END mask alone, and returns at once. The handler records 1, reads T's status
// and signals the semaphore, records 3 and returns. The waiter W waits on the semaphore and records
// 2 once its wait returns. Two cases:
// - W at 0x60, above T: the signal should switch to W before it returns, so 1 2 3
// - W at 0x80, below T (the control): W stays ready until T ends, so 1 3 2
//
// Both threads are pinned to core 0 and the main thread runs on core 2. Each case prints the
// signal's result, W's wait result, T's status in the handler, the runs of the handler with their
// type and whether each runs on T, and the steps. Nothing else runs in the handler, so it only
// signals, records and returns.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define TARGET_PRIORITY 0x70
#define MAX_STEPS 4
#define MAX_RUNS 4

static SceUID sema, target, handler_id;
static volatile int handler_status, signal_result, waiter_result;
static volatile int run_count, run_types[MAX_RUNS], run_on_target[MAX_RUNS];
static int step_count, steps[MAX_STEPS];

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < MAX_STEPS)
        steps[i] = step;
}

// Notes the type of a handler run, and whether it runs on the thread the event is for
static void note_run(SceInt32 type, SceUID thread_id) {
    const int n = __atomic_fetch_add(&run_count, 1, __ATOMIC_SEQ_CST);
    if (n < MAX_RUNS) {
        run_types[n] = type;
        run_on_target[n] = sceKernelGetThreadId() == thread_id;
    }
}

// The END handler of T, which runs in T's context once it returns
static int end_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)arg;
    (void)common;
    note_run(type, thread_id);
    record(1);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread_id, &info);
    handler_status = info.status;
    signal_result = sceKernelSignalSema(sema, 1);
    record(3);
    return 0;
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Waits on the semaphore, then records that its wait returned
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    waiter_result = sceKernelWaitSema(sema, 1, &timeout);
    record(2);
    return 0;
}

static void run_case(const char *name, int waiter_priority) {
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = -1;
    handler_status = signal_result = waiter_result = 0xDEAD;
    run_count = 0;
    for (int i = 0; i < MAX_RUNS; i++)
        run_types[i] = run_on_target[i] = -1;

    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID waiter =
        sceKernelCreateThread("waiter", waiter_entry, waiter_priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(waiter, 0, NULL);
    test_wait_until_waiting(waiter);

    target =
        sceKernelCreateThread("target", target_entry, TARGET_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    handler_id =
        sceKernelRegisterThreadEventHandler("end", target, SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, end_handler, NULL);
    PRINTF(test_created(handler_id), "%s: register the END handler", name);
    sceKernelStartThread(target, 0, NULL);

    // Joined first, so every record the threads made is complete
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(target, NULL, &timeout);
    test_join_thread(waiter);
    PRINTF(sceKernelUnregisterThreadEventHandler(handler_id), "%s: unregister the handler", name);
    sceKernelDeleteThread(target);
    sceKernelDeleteSema(sema);

    PRINTF(signal_result, "%s: the signal in the handler", name);
    PRINTF(handler_status, "%s: T's status in the handler", name);
    PRINTF(waiter_result, "%s: the waiter's result", name);
    PRINTF(step_count, "%s: steps recorded", name);
    PRINTF(run_count, "%s: runs of the handler", name);
    for (int i = 0; i < run_count && i < MAX_RUNS; i++) {
        PRINTF(run_types[i], "%s: run %d: the type", name, i);
        PRINTF(run_on_target[i], "%s: run %d: it runs on T", name, i);
    }
    const int count = step_count < MAX_STEPS ? step_count : MAX_STEPS;
    for (int i = 0; i < count; i++)
        PRINTF(steps[i], "%s: step %d", name, i);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("waiter at 0x60, above T", 0x60);
    run_case("waiter at 0x80, below T", 0x80);

    test_exit();
    return 0;
}
