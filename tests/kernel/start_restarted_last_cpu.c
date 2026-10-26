// Which core a thread runs on when it is started again after it ended, with cores 1 and 2 idle. The
// thread may run on cores 1 and 2, and it waits on a semaphore before each of its two runs. Before
// each wait ends, the main thread narrows the thread's mask to one core, so the first run is on the
// first core of the case and the second run is on the second core. Once the thread ends, the main
// thread widens its mask again while it is dormant, and starts it again from core 0. The main
// thread runs on core 0 throughout. Each case prints the core of both runs, the last CPU the info
// reports after the thread ends, the affinity and the core of the restarted run, and whether the
// restarted run is on the last CPU:
// - ran on core 2, then core 1: the thread ends after its run on core 1
// - ran on core 1, then core 2: the thread ends after its run on core 2
// Each case runs with the thread at 0x70 and at 0xA0, with a fresh thread and semaphore.
//
// The first start of the thread is not checked, since the core of that start is not part of the
// output.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CORES_1_AND_2 (SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2)

static SceUID sema;
// Set by the main thread before the restart, so the entry knows it runs again
static volatile int restarting;
// How many of the two runs have recorded their core, and what the runs and the restart recorded
static volatile int runs, first_cpu, second_cpu, restart_cpu, restart_mask;

// Waits for the semaphore before each of its two runs and records the core of each. When it runs
// again after a restart it only records its core and its affinity mask.
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    if (restarting) {
        restart_cpu = sceKernelGetCpuId();
        restart_mask = sceKernelGetThreadCpuAffinityMask(0);
        return 0;
    }
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    first_cpu = sceKernelGetCpuId();
    runs = 1;
    timeout = TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    second_cpu = sceKernelGetCpuId();
    runs = 2;
    return 0;
}

struct Case {
    const char *name;
    int first_core, second_core;
};

static const struct Case cases[] = {
    { "ran on core 2, then core 1", 2, 1 },
    { "ran on core 1, then core 2", 1, 2 },
};

static void run_case(const struct Case *c, int priority) {
    runs = restarting = 0;
    first_cpu = second_cpu = restart_cpu = restart_mask = -2;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID thread = sceKernelCreateThread("t", t_entry, priority, 0x4000, 0, CORES_1_AND_2, NULL);
    sceKernelStartThread(thread, 0, NULL);

    // Each narrowing happens while the thread waits, so the wake that follows can only use that core
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_SEMA, sema));
    sceKernelChangeThreadCpuAffinityMask(thread, SCE_KERNEL_CPU_MASK_USER_0 << c->first_core);
    sceKernelSignalSema(sema, 1);
    TEST_WAIT_FOR(runs == 1);

    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_SEMA, sema));
    sceKernelChangeThreadCpuAffinityMask(thread, SCE_KERNEL_CPU_MASK_USER_0 << c->second_core);
    sceKernelSignalSema(sema, 1);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);

    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    const int last_cpu = info.lastExecutedCpuId;
    sceKernelChangeThreadCpuAffinityMask(thread, CORES_1_AND_2);
    restarting = 1;
    sceKernelStartThread(thread, 0, NULL);
    timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);

    PRINTF(first_cpu, "%s, T at 0x%X: the core of the first run", c->name, priority);
    PRINTF(second_cpu, "%s, T at 0x%X: the core of the second run", c->name, priority);
    PRINTF(last_cpu, "%s, T at 0x%X: the last CPU after it ends", c->name, priority);
    PRINTF(restart_mask, "%s, T at 0x%X: the affinity of the restarted run", c->name, priority);
    PRINTF(restart_cpu, "%s, T at 0x%X: the core of the restarted run", c->name, priority);
    PRINTF(restart_cpu == last_cpu, "%s, T at 0x%X: the restarted run is on the last CPU", c->name, priority);
    sceKernelDeleteThread(thread);
    sceKernelDeleteSema(sema);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    TEST_WAIT_FOR(sceKernelGetCpuId() == 0);
    static const int priorities[2] = { 0x70, 0xA0 };
    for (int p = 0; p < 2; p++)
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
            run_case(&cases[i], priorities[p]);
    test_exit();
    return 0;
}
