// Where a thread that may run on cores 0 and 1 waits when a signal wakes it while spinners at its
// own priority hold both cores. T last ran on core 0 and waits on a semaphore there, then its mask
// becomes cores 0 and 1. A spinner at T's priority holds each core and loops without syscalls
// until it is stopped. The main thread, on core 2 at a higher priority, signals T, waits 300 ms and
// notes T's status and whether it ran. Then the spinner on core 1 stops, and after 300 ms T's run
// and core are noted, then the spinner on core 0 stops, and the same is noted again. The core is
// printed as -1 while T hasn't run. Cases: T at 0x70, and T at 0xA0.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define SPINS 400000000
#define TWO_CORES (SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_1)

static SceUID sema, spinners[2];
static volatile int spinner_up[2], spinner_stop[2], t_cpu, t_ran;

static int spinner_entry(SceSize args, void *argp) {
    (void)args;
    const int core = *(const int *)argp;
    spinner_up[core] = 1;
    for (volatile int n = 0; n < SPINS && !spinner_stop[core]; n++)
        ;
    return 0;
}

static void spinner_start(int core, int priority) {
    spinner_up[core] = spinner_stop[core] = 0;
    const SceUInt32 mask = core ? SCE_KERNEL_CPU_MASK_USER_1 : SCE_KERNEL_CPU_MASK_USER_0;
    spinners[core] = sceKernelCreateThread("spinner", spinner_entry, priority, 0x4000, 0, mask, NULL);
    sceKernelStartThread(spinners[core], sizeof(core), &core);
    TEST_WAIT_FOR(spinner_up[core]);
}

static void spinner_end(int core) {
    spinner_stop[core] = 1;
    sceKernelWaitThreadEnd(spinners[core], NULL, NULL);
    sceKernelDeleteThread(spinners[core]);
}

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    t_cpu = info.currentCpuId;
    t_ran = 1;
    return 0;
}

static void run_case(int priority) {
    t_ran = 0;
    t_cpu = -2;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID t = sceKernelCreateThread("t", t_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    test_wait_until_waiting(t);
    sceKernelChangeThreadCpuAffinityMask(t, TWO_CORES);
    spinner_start(0, priority);
    spinner_start(1, priority);
    sceKernelSignalSema(sema, 1);
    sceKernelDelayThread(300000);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(t, &info);
    const int ran_busy = t_ran;
    PRINTF(info.status, "T at 0x%X, spinners on cores 0 and 1: T's status 300 ms after the signal", priority);
    PRINTF(ran_busy, "T at 0x%X, spinners on cores 0 and 1: T ran within 300 ms of the signal", priority);

    spinner_end(1);
    sceKernelDelayThread(300000);
    const int ran_core_1 = t_ran;
    PRINTF(ran_core_1, "T at 0x%X, core 1's spinner stopped: T ran within 300 ms", priority);
    PRINTF(ran_core_1 ? t_cpu : -1, "T at 0x%X, core 1's spinner stopped: the core T ran on", priority);

    spinner_end(0);
    sceKernelDelayThread(300000);
    const int ran_core_0 = t_ran;
    PRINTF(ran_core_0, "T at 0x%X, core 0's spinner stopped: T ran within 300 ms", priority);
    PRINTF(ran_core_0 ? t_cpu : -1, "T at 0x%X, core 0's spinner stopped: the core T ran on", priority);

    // Joined first, so every record the thread made is complete
    TEST_WAIT_FOR(t_ran);
    test_join_thread(t);
    sceKernelDeleteSema(sema);
}

int main(void) {
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);
    run_case(0x70);
    run_case(0xA0);
    test_exit();
    return 0;
}
