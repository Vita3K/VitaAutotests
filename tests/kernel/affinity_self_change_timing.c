// Where a thread that changes its own CPU mask to core 1 alone runs, read at
// four points after the change: right after the call, after
// sceKernelGetThreadId(), after a delay of 1 us and after a delay of 1 ms. The
// thread T starts with mask core 0, so it runs on core 0, and the main thread,
// on core 2, blocks on a gate semaphore that T signals as it finishes. Nothing
// else runs on cores 0 and 1 meanwhile. T runs at 0x70 and at 0xA0, one case
// each. Each case prints the change's result and the core T read at each point.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define READS 4

static SceUID gate;
static volatile int change_result, cores[READS];

static int self_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceKernelThreadInfo info = {.size = sizeof(info)};
    change_result = sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    sceKernelGetThreadInfo(0, &info);
    cores[0] = info.currentCpuId;
    (void)sceKernelGetThreadId();
    sceKernelGetThreadInfo(0, &info);
    cores[1] = info.currentCpuId;
    sceKernelDelayThread(1);
    sceKernelGetThreadInfo(0, &info);
    cores[2] = info.currentCpuId;
    sceKernelDelayThread(1000);
    sceKernelGetThreadInfo(0, &info);
    cores[3] = info.currentCpuId;
    sceKernelSignalSema(gate, 1);
    return 0;
}

static void run_case(int priority) {
    change_result = -1;
    for (int i = 0; i < READS; i++)
        cores[i] = -2;
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    const SceUID t = sceKernelCreateThread("self", self_entry, priority, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    test_join_thread(t);
    sceKernelDeleteSema(gate);
    PRINTF(change_result, "T at 0x%X: the change to core 1", priority);
    PRINTF(cores[0], "T at 0x%X: its core right after the change", priority);
    PRINTF(cores[1], "T at 0x%X: its core after sceKernelGetThreadId()", priority);
    PRINTF(cores[2], "T at 0x%X: its core after a delay of 1 us", priority);
    PRINTF(cores[3], "T at 0x%X: its core after a delay of 1 ms", priority);
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
