// Whether a thread that runs on core 0 keeps counting once its mask is narrowed to core 1, which a
// spinner at the same priority holds. L runs on core 0 in a loop that notes the core it runs on,
// read from its own thread info, and counts. The spinner runs on core 1 without syscalls until it
// is stopped. The main thread, on core 2, narrows L's mask to core 1, waits 300 ms and notes L's
// status and whether its count moved. Then the spinner stops, and after 300 ms the count and the
// core L notes are read again. Cases: L and the spinner at 0x70, and at 0xA0.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define SPINS 400000000

static volatile int l_up, l_stop, l_cpu, spin_up, spin_stop;
static volatile unsigned int l_count;

static int l_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    l_up = 1;
    while (!l_stop) {
        SceKernelThreadInfo info = { .size = sizeof(info) };
        sceKernelGetThreadInfo(0, &info);
        l_cpu = info.currentCpuId;
        l_count++;
    }
    return 0;
}

static int spin_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    spin_up = 1;
    for (volatile int n = 0; n < SPINS && !spin_stop; n++)
        ;
    return 0;
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

static void run_case(int priority) {
    l_up = l_stop = l_cpu = spin_up = spin_stop = 0;
    l_count = 0;
    const SceUID l = sceKernelCreateThread("l", l_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(l, 0, NULL);
    TEST_WAIT_FOR(l_up && l_count > 0);
    const SceUID spinner = sceKernelCreateThread("spin", spin_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(spinner, 0, NULL);
    TEST_WAIT_FOR(spin_up);
    const int result = sceKernelChangeThreadCpuAffinityMask(l, SCE_KERNEL_CPU_MASK_USER_1);
    const unsigned int count = l_count;
    sceKernelDelayThread(300000);
    const int moved = l_count != count;
    const SceUInt32 l_status = status_of(l);
    PRINTF(result >= 0, "L and the spinner at 0x%X, L narrowed to core 1: the change", priority);
    PRINTF(l_status, "L and the spinner at 0x%X, L narrowed to core 1: L's status 300 ms later", priority);
    PRINTF(moved, "L and the spinner at 0x%X, L narrowed to core 1: L's count moved in 300 ms", priority);

    const unsigned int count_stopped = l_count;
    spin_stop = 1;
    sceKernelWaitThreadEnd(spinner, NULL, NULL);
    sceKernelDeleteThread(spinner);
    sceKernelDelayThread(300000);
    const int counts_again = l_count != count_stopped;
    PRINTF(counts_again, "L and the spinner at 0x%X, spinner stopped: L counts again in 300 ms", priority);
    PRINTF(l_cpu, "L and the spinner at 0x%X, spinner stopped: the core L notes 300 ms later", priority);

    l_stop = 1;
    test_join_thread(l);
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
