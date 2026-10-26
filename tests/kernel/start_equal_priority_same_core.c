// Whether a thread that a running thread starts runs before the starter's next line, when it has
// the starter's priority. The starter S at 0x70 records 0, starts the thread T, records 1 once the
// start returns, then waits on a semaphore that T signals and records 3 when the wait returns. T
// records 2 and signals the semaphore. Both are pinned to core 0, and the main thread on core 2
// joins them. Cases:
// - T at 0x70, the same as S: the start should leave S running, so the steps are 0 1 2 3
// - T at 0x60, above S (a control): the start should switch to T, so the steps are 0 2 1 3
// - T at 0x80, below S (a control): T stays ready until S waits, so the steps are 0 1 2 3
//
// Each case prints the result of the start, the result of S's wait for T's signal, and the steps in
// the order they were recorded.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define STARTER_PRIORITY 0x70
#define MAX_STEPS 4

static SceUID target, starter, signal_sema;
static int step_count, steps[MAX_STEPS];
static volatile int start_result, wait_result;

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < MAX_STEPS)
        steps[i] = step;
}

// Records 2, then signals the starter
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    record(2);
    sceKernelSignalSema(signal_sema, 1);
    return 0;
}

// Starts T, records its line, then waits for T's signal and records its line after the wait
static int starter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    record(0);
    start_result = sceKernelStartThread(target, 0, NULL);
    record(1);
    SceUInt timeout = TEST_WAIT_US;
    wait_result = sceKernelWaitSema(signal_sema, 1, &timeout);
    record(3);
    return 0;
}

static void print_steps(const char *name) {
    const int count = step_count < MAX_STEPS ? step_count : MAX_STEPS;
    PRINTF(step_count, "%s: steps recorded", name);
    for (int i = 0; i < count; i++)
        PRINTF(steps[i], "%s: step %d", name, i);
}

static void run_case(const char *name, int target_priority) {
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = -1;
    start_result = wait_result = 0xDEAD;
    signal_sema = sceKernelCreateSema("signal", 0, 0, 1, NULL);
    target =
        sceKernelCreateThread("target", target_entry, target_priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    starter =
        sceKernelCreateThread("starter", starter_entry, STARTER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(starter, 0, NULL);

    // Joined first, so every record the threads made is complete
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(starter, NULL, &timeout);
    timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(target, NULL, &timeout);
    sceKernelDeleteThread(starter);
    sceKernelDeleteThread(target);
    sceKernelDeleteSema(signal_sema);

    PRINTF(start_result, "%s: the start", name);
    PRINTF(wait_result, "%s: the starter's wait for T's signal", name);
    print_steps(name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("T at 0x70, the same as the starter", 0x70);
    run_case("T at 0x60, above the starter", 0x60);
    run_case("T at 0x80, below the starter", 0x80);

    test_exit();
    return 0;
}
