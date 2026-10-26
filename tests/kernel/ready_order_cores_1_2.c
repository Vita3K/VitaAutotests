// The order threads that are ready on core 1, then on core 2, run in once the core lets go of a
// gate. On each core, a gate thread at the highest priority, pinned to that core, spins without
// syscalls until the main thread on core 0 lifts it. While it spins, the main thread starts a
// thread at 0x98 first, then three threads at 0x90 in the order 2, 0, 1, all pinned to that core.
// Every thread records its number when it runs, so the same priority threads should run in the
// order they were started, 2, 0 and 1, and the 0x98 thread (number 3) after them. The main thread
// runs on core 0 and never shares a core with the threads under test.
//
// Each core prints its steps in the order they were recorded.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define PRIORITY 0x90
#define LOW_PRIORITY 0x98
#define MAX_STEPS 4
// A few seconds on a Vita at most, the gate normally lets go long before
#define SPINS 400000000

static volatile int gate_up, gate_open;
static int step_count, steps[MAX_STEPS];

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < MAX_STEPS)
        steps[i] = step;
}

static int gate_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    gate_up = 1;
    for (volatile int n = 0; n < SPINS && !gate_open; n++)
        ;
    return 0;
}

// Records its number
static int record_entry(SceSize args, void *argp) {
    (void)args;
    record(*(const int *)argp);
    return 0;
}

static SceUID create(SceKernelThreadEntry entry, int priority, SceUInt32 mask) {
    return sceKernelCreateThread("order", entry, priority, 0x4000, 0, mask, NULL);
}

static void start(SceUID thread, int id) { sceKernelStartThread(thread, sizeof(id), &id); }

static void join(SceUID *threads, int count) {
    for (int i = 0; i < count; i++) {
        SceUInt timeout = 3 * TEST_WAIT_US;
        sceKernelWaitThreadEnd(threads[i], NULL, &timeout);
        sceKernelDeleteThread(threads[i]);
    }
}

static void print_steps(const char *name) {
    const int count = step_count < MAX_STEPS ? step_count : MAX_STEPS;
    PRINTF(step_count, "%s: steps recorded", name);
    for (int i = 0; i < count; i++)
        PRINTF(steps[i], "%s: step %d", name, i);
}

// Runs one core's case: the gate holds the core while the threads are started, then lets go
static void run_core(const char *name, SceUInt32 mask) {
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = -1;
    gate_up = gate_open = 0;

    const SceUID gate = create(gate_entry, SCE_KERNEL_HIGHEST_PRIORITY_USER, mask);
    sceKernelStartThread(gate, 0, NULL);
    TEST_WAIT_FOR(gate_up);

    SceUID threads[4];
    for (int i = 0; i < 3; i++)
        threads[i] = create(record_entry, PRIORITY, mask);
    threads[3] = create(record_entry, LOW_PRIORITY, mask);
    start(threads[3], 3);
    start(threads[2], 2);
    start(threads[0], 0);
    start(threads[1], 1);

    gate_open = 1;
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(gate, NULL, &timeout);
    sceKernelDeleteThread(gate);
    join(threads, 4);
    print_steps(name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_core("core 1", SCE_KERNEL_CPU_MASK_USER_1);
    run_core("core 2", SCE_KERNEL_CPU_MASK_USER_2);

    test_exit();
    return 0;
}
