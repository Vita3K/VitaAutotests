// Where a thread at 0x70 runs when its CPU mask is narrowed to core 1 while it is waiting or
// sleeping on core 0. The thread T starts with mask core 0, so it waits on core 0. In the waiting
// cases the main thread, on core 2, widens T's mask to cores 0 and 1 and then narrows it to core 1
// alone, and then signals T. The cases:
// - waiting on a semaphore, core 1 idle
// - waiting on a semaphore, core 1 busy with a spinner at 0xB0 that runs until the case has ended
// - in a 500 ms delay on core 0, with the mask narrowed to core 1 during the delay, so the delay
//   ends after the change
// Each case prints the results of its mask changes, T's status after the narrowing and before it
// runs, and the core and status T reads for itself when it runs.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define T_PRIORITY 0x70
#define SPINNER_PRIORITY 0xB0
#define TWO_CORES (SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_1)
#define DELAY_US 500000
#define WAIT 1
#define DELAY 2

static SceUID sema;
static volatile int t_ran, t_cpu, t_status, spinner_up, spinner_stop;

// Waits for the semaphore or delays, as its argument says, then records where
// it runs
static int t_entry(SceSize args, void *argp) {
    (void)args;
    const int mode = *(const int *)argp;
    if (mode == WAIT) {
        SceUInt timeout = 3 * TEST_WAIT_US;
        sceKernelWaitSema(sema, 1, &timeout);
    } else {
        sceKernelDelayThread(DELAY_US);
    }
    SceKernelThreadInfo info = {.size = sizeof(info)};
    sceKernelGetThreadInfo(0, &info);
    t_cpu = info.currentCpuId;
    t_status = info.status;
    t_ran = 1;
    return 0;
}

static int spinner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    spinner_up = 1;
    while (!spinner_stop)
        ;
    return 0;
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = {.size = sizeof(info)};
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

static void report(const char *name, int narrowed, SceUInt32 before) {
    PRINTF(narrowed, "%s: the change to core 1 alone", name);
    PRINTF(before, "%s: T's status before it runs", name);
    PRINTF(t_ran, "%s: T ran", name);
    PRINTF(t_cpu, "%s: the core T ran on", name);
    PRINTF(t_status, "%s: T's status once it ran", name);
}

static void wait_case(const char *name, int busy_core_1) {
    t_ran = 0;
    t_cpu = -2;
    t_status = -1;
    spinner_up = spinner_stop = 0;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID t = sceKernelCreateThread("t", t_entry, T_PRIORITY, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    int mode = WAIT;
    sceKernelStartThread(t, sizeof(mode), &mode);
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, sema));
    const int widened = sceKernelChangeThreadCpuAffinityMask(t, TWO_CORES);
    SceUID spinner = 0;
    if (busy_core_1) {
        spinner = sceKernelCreateThread("spinner", spinner_entry, SPINNER_PRIORITY, 0x4000, 0,
                                        SCE_KERNEL_CPU_MASK_USER_1, NULL);
        sceKernelStartThread(spinner, 0, NULL);
        TEST_WAIT_FOR(spinner_up);
    }
    const int narrowed = sceKernelChangeThreadCpuAffinityMask(t, SCE_KERNEL_CPU_MASK_USER_1);
    const SceUInt32 before = status_of(t);
    sceKernelSignalSema(sema, 1);
    TEST_WAIT_FOR(t_ran);
    if (busy_core_1) {
        spinner_stop = 1;
        test_join_thread(spinner);
    }
    test_join_thread(t);
    sceKernelDeleteSema(sema);
    PRINTF(widened, "%s: the change to cores 0 and 1", name);
    report(name, narrowed, before);
}

static void delay_case(void) {
    t_ran = 0;
    t_cpu = -2;
    t_status = -1;
    const SceUID t = sceKernelCreateThread("t", t_entry, T_PRIORITY, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    int mode = DELAY;
    sceKernelStartThread(t, sizeof(mode), &mode);
    test_wait_until_waiting(t);
    const int narrowed = sceKernelChangeThreadCpuAffinityMask(t, SCE_KERNEL_CPU_MASK_USER_1);
    const SceUInt32 before = status_of(t);
    TEST_WAIT_FOR(t_ran);
    test_join_thread(t);
    report("in a delay, narrowed during it", narrowed, before);
}

int main(void) {
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);
    wait_case("waiting, core 1 idle", 0);
    wait_case("waiting, core 1 busy at 0xB0", 1);
    delay_case();
    test_exit();
    return 0;
}
