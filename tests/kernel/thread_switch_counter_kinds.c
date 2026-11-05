// Which counter of thread info grows for each way a thread leaves a core. The test thread T runs
// at 0x80 on core 0 and reads its own counters before and after each case. The main thread waits
// on core 2 and starts nothing else on cores 0 and 1. Case a: T signals a semaphore five times
// that a thread H at 0x60 on core 0 waits on, so H runs and T is preempted from inside the call
// each time. Case b: T takes a semaphore that already has a count, so it doesn't block. Case c:
// T waits on a semaphore that main signals, so T blocks once. Case d: T changes its own mask from
// core 0 to core 1, where nothing else runs. Each case prints the deltas of the interrupt
// preempt, thread preempt, release and CPU change counts.
//
// On a PS Vita TV on firmware 3.65 H wakes once per signal. In case a the thread preempt count
// grows by 5 and the others by 0. In case b none grows. In case c the release count grows by 1
// and the others by 0. In case d the interrupt preempt count and the CPU change count grow by 1
// each and the other two by 0, the call returns the old mask 0x10000, and T is on core 1 after
// it.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define T_PRIORITY 0x80
#define H_PRIORITY 0x60
#define MAIN_PRIORITY 0xA0
#define SIGNALS 5
// How long a spin waits for the core change at most
#define SPIN_CAP_US 1000000

// The counters of thread info that the test compares
typedef struct Counters {
    SceUInt32 intr;
    SceUInt32 preempt;
    SceUInt32 release;
    SceInt32 change_cpu;
} Counters;

// The change of the counters over one case
typedef struct Delta {
    int intr, preempt, release, change_cpu;
} Delta;

static SceUID h_sema, take_sema, block_sema;
static volatile int woke, h_quit, change_result, core_after_change;
static Delta delta_a, delta_b, delta_c, delta_d;

// Reads the counters of the calling thread
static Counters read_counters(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    return (Counters){ info.intrPreemptCount, info.threadPreemptCount, info.threadReleaseCount, info.changeCpuCount };
}

static Delta since(Counters start) {
    const Counters now = read_counters();
    return (Delta){ (int)(now.intr - start.intr), (int)(now.preempt - start.preempt), (int)(now.release - start.release), now.change_cpu - start.change_cpu };
}

// Waits on h_sema and counts each wake, until the main thread asks it to quit
static int h_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    while (!h_quit) {
        SceUInt timeout = 3 * TEST_WAIT_US;
        if (sceKernelWaitSema(h_sema, 1, &timeout) == 0 && !h_quit)
            woke++;
    }
    return 0;
}

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    Counters start = read_counters();
    for (int i = 0; i < SIGNALS; i++)
        sceKernelSignalSema(h_sema, 1);
    delta_a = since(start);

    start = read_counters();
    sceKernelWaitSema(take_sema, 1, NULL);
    delta_b = since(start);

    start = read_counters();
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(block_sema, 1, &timeout);
    delta_c = since(start);

    // The call returns on core 1, and the spin only covers a slow move
    start = read_counters();
    change_result = sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    const SceInt64 end = sceKernelGetSystemTimeWide() + SPIN_CAP_US;
    while (sceKernelGetCpuId() != 1 && sceKernelGetSystemTimeWide() < end)
        ;
    core_after_change = sceKernelGetCpuId();
    delta_d = since(start);
    return 0;
}

static void print_delta(const char *label, Delta d) {
    PRINTF(d.intr, "%s: the interrupt preempt count grew by", label);
    PRINTF(d.preempt, "%s: the thread preempt count grew by", label);
    PRINTF(d.release, "%s: the release count grew by", label);
    PRINTF(d.change_cpu, "%s: the CPU change count grew by", label);
}

int main(void) {
    const int orig_priority = sceKernelGetThreadCurrentPriority();
    const int orig_mask = sceKernelGetThreadCpuAffinityMask(0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);

    h_sema = sceKernelCreateSema("h", 0, 0, SIGNALS, NULL);
    take_sema = sceKernelCreateSema("take", 0, 1, 1, NULL);
    block_sema = sceKernelCreateSema("block", 0, 0, 1, NULL);
    const SceUID h = sceKernelCreateThread("h", h_entry, H_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID t = sceKernelCreateThread("t", t_entry, T_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(h, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(h, WAIT_TYPE_SEMA, h_sema));
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, block_sema));
    sceKernelSignalSema(block_sema, 1);
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(t, NULL, &timeout);

    PRINT("H woke once per signal", woke == SIGNALS);
    print_delta("five signals that wake a higher priority thread on the same core", delta_a);
    print_delta("a wait that takes a count at once", delta_b);
    print_delta("a wait that blocks", delta_c);
    PRINT("the self change of the mask: the old mask it returns", change_result);
    PRINT("the self change of the mask: the core after it", core_after_change);
    print_delta("the self change of the mask from core 0 to core 1", delta_d);

    h_quit = 1;
    sceKernelSignalSema(h_sema, 1);
    timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(h, NULL, &timeout);
    sceKernelDeleteThread(h);
    sceKernelDeleteThread(t);
    sceKernelDeleteSema(h_sema);
    sceKernelDeleteSema(take_sema);
    sceKernelDeleteSema(block_sema);
    sceKernelChangeThreadCpuAffinityMask(0, orig_mask);
    sceKernelChangeThreadPriority(0, orig_priority);
    test_exit();
    return 0;
}
