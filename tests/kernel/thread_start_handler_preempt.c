// Whether a thread that a running thread starts runs before the starter's next line, and where its
// START thread event handler runs in that. The thread T is created and given a START handler by the
// main thread, then the starter S at 0x70 starts it and records a line after sceKernelStartThread
// returns. Both are pinned to core 0, and the main thread runs on core 2 and joins them. Two cases:
// - T at 0x60, above S: the start should switch to T, so the handler and T's entry run before S's
//   line
// - T at 0x80, below S (the control): T stays ready until S ends, so S's line comes first
//
// Each record is a step: 1 for the handler, 2 for T's entry and 3 for S's line. Each case prints
// the result of the start, the runs of the handler with their type and whether each runs on T, and
// the steps in the order they were recorded.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define STARTER_PRIORITY 0x70
#define MAX_STEPS 4
#define MAX_RUNS 4

static SceUID target, starter;
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

// The START handler of T, which runs before its entry
static int start_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)arg;
    (void)common;
    note_run(type, thread_id);
    record(1);
    return 0;
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    record(2);
    return 0;
}

static volatile int start_result;

// Starts T, then records its line
static int starter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    start_result = sceKernelStartThread(target, 0, NULL);
    record(3);
    return 0;
}

static void run_case(const char *name, int target_priority) {
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = -1;
    start_result = 0xDEAD;
    run_count = 0;
    for (int i = 0; i < MAX_RUNS; i++)
        run_types[i] = run_on_target[i] = -1;

    target =
        sceKernelCreateThread("target", target_entry, target_priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID handler =
        sceKernelRegisterThreadEventHandler("start", target, SCE_KERNEL_THREAD_EVENT_TYPE_START, start_handler, NULL);
    PRINTF(test_created(handler), "%s: register the START handler", name);
    starter =
        sceKernelCreateThread("starter", starter_entry, STARTER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(starter, 0, NULL);

    // Joined first, so every record the threads made is complete
    test_join_thread(starter);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(target, NULL, &timeout);
    PRINTF(sceKernelUnregisterThreadEventHandler(handler), "%s: unregister the handler", name);
    sceKernelDeleteThread(target);

    PRINTF(start_result, "%s: the start", name);
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

    run_case("T at 0x60, above the starter", 0x60);
    run_case("T at 0x80, below the starter", 0x80);

    test_exit();
    return 0;
}
