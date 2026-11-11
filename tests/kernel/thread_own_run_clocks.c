// What the run clocks of a thread's own info report while the thread keeps its core. The thread T
// is the only one on core 1 and reads its own run clocks, spins for 300 ms, and reads them again.
// Then it waits on a semaphore, and the main thread, which runs on core 0, reads the run clocks of
// T while it waits. The test prints whether T's two reads are equal, and whether the read from
// outside is above T's first read by at least 250 ms.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define SPIN_US 300000
#define GROWTH_US 250000

static SceUID rest;
static volatile int spun;
static SceUInt64 first_read, second_read;

static SceUInt64 own_run_clocks(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    return info.runClocks;
}

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    first_read = own_run_clocks();
    const SceInt64 start = sceKernelGetSystemTimeWide();
    while (sceKernelGetSystemTimeWide() - start < SPIN_US) {
    }
    second_read = own_run_clocks();
    spun = 1;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(rest, 1, &timeout);
    return 0;
}

int main(void) {
    const int orig_mask = sceKernelGetThreadCpuAffinityMask(0);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    TEST_WAIT_FOR(sceKernelGetCpuId() == 0);

    rest = sceKernelCreateSema("rest", 0, 0, 1, NULL);
    const SceUID t = sceKernelCreateThread("t", t_entry, 0x60, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(spun);
    test_wait_until_waiting(t);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    const int result = sceKernelGetThreadInfo(t, &info);

    PRINT("read the info of the waiting thread", result);
    PRINT("the two reads of its own run clocks are equal", first_read == second_read);
    PRINT("the read from outside is above the first by at least 250 ms", (SceInt64)(info.runClocks - first_read) >= GROWTH_US);

    sceKernelSignalSema(rest, 1);
    sceKernelWaitThreadEnd(t, NULL, NULL);
    sceKernelDeleteThread(t);
    sceKernelDeleteSema(rest);
    sceKernelChangeThreadCpuAffinityMask(0, orig_mask);
    test_exit();
    return 0;
}
