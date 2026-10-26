// Which core a thread that may run on cores 0 and 1 takes when a signal from core 2 wakes it, while
// a spinner at the thread's own priority holds core 0 and core 1 is idle. T last ran on core 0,
// since it first waits on a semaphore there, then its mask becomes cores 0 and 1. The spinner loops
// without syscalls until it is stopped. The main thread, on core 2, signals the semaphore and waits
// 300 ms, then notes whether T ran. After that the spinner stops, and once T has ended the core T
// ran on is noted. Cases: T at 0x70, and T at 0xA0, which shares its queue with the spinner.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define SPINS 400000000
#define TWO_CORES (SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_1)

static SceUID sema, spinner;
static volatile int spinner_up, spinner_stop, t_cpu, t_ran;

static int spinner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    spinner_up = 1;
    for (volatile int n = 0; n < SPINS && !spinner_stop; n++)
        ;
    return 0;
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
    spinner_up = spinner_stop = 0;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID t = sceKernelCreateThread("t", t_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    test_wait_until_waiting(t);
    sceKernelChangeThreadCpuAffinityMask(t, TWO_CORES);
    spinner = sceKernelCreateThread("spinner", spinner_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(spinner, 0, NULL);
    TEST_WAIT_FOR(spinner_up);
    sceKernelSignalSema(sema, 1);
    sceKernelDelayThread(300000);
    const int ran = t_ran;
    spinner_stop = 1;
    sceKernelWaitThreadEnd(spinner, NULL, NULL);
    sceKernelDeleteThread(spinner);
    // Joined first, so every record the thread made is complete
    TEST_WAIT_FOR(t_ran);
    test_join_thread(t);
    sceKernelDeleteSema(sema);
    PRINTF(ran, "T at 0x%X, spinner on core 0, core 1 idle: T ran within 300 ms of the signal", priority);
    PRINTF(t_cpu, "T at 0x%X, spinner on core 0, core 1 idle: the core T ran on", priority);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);
    run_case(0x70);
    run_case(0xA0);
    test_exit();
    return 0;
}
