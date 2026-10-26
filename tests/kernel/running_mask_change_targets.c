// Where a RUNNING thread goes when another core narrows its mask to one core, which another thread
// spins on at a higher or a lower priority. L runs on core 0 in a loop that notes the core it runs
// on, read from its own thread info, and counts. H spins on core 1 without syscalls, counting,
// until it is stopped. The main thread, on core 2, narrows L's mask to core 1, waits 300 ms and
// notes L's status. In the next 300 ms it notes whether L's count and H's count moved. With H of a
// higher priority, H is then stopped, and the next 300 ms show whether L counts. With H of a lower
// priority, H is not stopped until L has ended, and the same window shows whether L keeps counting.
// Cases: L at 0x70 and at 0xA0, each with H at 0x50 (higher) and at 0xB0 (lower).
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define SPINS 400000000
#define WINDOW_US 300000

static volatile int l_up, l_stop, l_cpu, h_up, h_stop;
static volatile unsigned int l_count, h_count;

// L, which notes the core it runs on and counts in each loop
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

// H, which counts until it is stopped
static int h_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    h_up = 1;
    for (int n = 0; n < SPINS && !h_stop; n++)
        h_count++;
    return 0;
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

static void run_case(int l_priority, int h_priority) {
    l_up = l_stop = l_cpu = h_up = h_stop = 0;
    l_count = h_count = 0;
    const int higher = h_priority < l_priority;
    const SceUID l = sceKernelCreateThread("l", l_entry, l_priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(l, 0, NULL);
    TEST_WAIT_FOR(l_up && l_count > 0);
    const SceUID h = sceKernelCreateThread("h", h_entry, h_priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(h, 0, NULL);
    TEST_WAIT_FOR(h_up);
    const int result = sceKernelChangeThreadCpuAffinityMask(l, SCE_KERNEL_CPU_MASK_USER_1);
    sceKernelDelayThread(WINDOW_US);
    const SceUInt32 l_status = status_of(l);
    const unsigned int l_before = l_count, h_before = h_count;
    sceKernelDelayThread(WINDOW_US);
    const int l_moved = l_count != l_before;
    const int h_moved = h_count != h_before;
    const unsigned int l_settled = l_count;

    // H ends only while it runs. The higher H is joined here, and the lower one, which can't run
    // while L holds core 1, is joined after L.
    h_stop = 1;
    if (higher)
        test_join_thread(h);
    sceKernelDelayThread(WINDOW_US);
    const int l_ran = l_count != l_settled;
    const int l_core = l_cpu;

    l_stop = 1;
    test_join_thread(l);
    if (!higher)
        test_join_thread(h);

    const int ready_or_standby = l_status == SCE_THREAD_READY || l_status == SCE_THREAD_STANDBY;
    PRINTF(result >= 0, "L at 0x%X, H at 0x%X: the mask change to core 1", l_priority, h_priority);
    PRINTF(ready_or_standby, "L at 0x%X, H at 0x%X: L READY or STANDBY 300 ms after", l_priority, h_priority);
    PRINTF(l_moved, "L at 0x%X, H at 0x%X: L's count moved in the next 300 ms", l_priority, h_priority);
    PRINTF(h_moved, "L at 0x%X, H at 0x%X: H's count moved in that time", l_priority, h_priority);
    PRINTF(l_ran, "L at 0x%X, H at 0x%X: L counted in the 300 ms after H stopped running", l_priority, h_priority);
    PRINTF(l_core, "L at 0x%X, H at 0x%X: the core L noted then", l_priority, h_priority);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);
    static const int l_priorities[2] = { 0x70, 0xA0 };
    // The first H is of a higher priority than L, the second of a lower one
    static const int h_priorities[2] = { 0x50, 0xB0 };
    for (int l = 0; l < 2; l++)
        for (int h = 0; h < 2; h++)
            run_case(l_priorities[l], h_priorities[h]);
    test_exit();
    return 0;
}
