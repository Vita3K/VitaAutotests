// Whether a core that frees up skips a READY thread in the queue that cores 0 and 1 share when the
// thread's mask leaves that core out, and runs the next thread its mask allows, even one of a
// lower priority. Two gate threads at 0x40 spin without syscalls, one on core 0 and one on core 1,
// and the main thread on core 2 makes two threads READY behind them: R1 with the mask of core 1
// alone, and R2 with the mask of cores 0 and 1. Core 0 is released first, so R2 is the only one of
// the two that may run there. Then core 1 is released, and R1 runs. Each pair of priorities is run
// as its own case: both threads at 0xA0, and R1 at 0x90 with R2 at 0xA0, so R1 is the higher
// priority of the two. Each case prints the status of both threads before the first release,
// whether R2 ran and R1 did not after it, whether R1 ran after the second release, and the order
// the threads ran in, as digits: 1 for R1 and 2 for R2.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_PRIORITY 0x40
#define MAIN_PRIORITY 0x60
#define STATUS_READY 0x2
#define BOTH_CORES (SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_1)
#define SPINS 400000000

static volatile int gate_up[2], stop_gate[2];
static SceUID gates[2];
static volatile int ran_count;
static int ran_order[2];

// Gate i spins on core i until told to stop
static int gate_entry(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    gate_up[i] = 1;
    for (volatile int n = 0; n < SPINS && !stop_gate[i]; n++)
        ;
    return 0;
}

static void start_gate(int i) {
    stop_gate[i] = gate_up[i] = 0;
    const SceUInt32 mask = i == 0 ? SCE_KERNEL_CPU_MASK_USER_0 : SCE_KERNEL_CPU_MASK_USER_1;
    gates[i] = sceKernelCreateThread("gate", gate_entry, GATE_PRIORITY, 0x4000, 0, mask, NULL);
    sceKernelStartThread(gates[i], sizeof(i), &i);
    TEST_WAIT_FOR(gate_up[i]);
}

// Waits for thread to end, for at most 3 seconds, then deletes it
static void join_or_timeout(SceUID thread) {
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);
}

static void release_gate(int i) {
    stop_gate[i] = 1;
    join_or_timeout(gates[i]);
}

// Records its id, 1 for R1 and 2 for R2, in the order the threads run
static int r_entry(SceSize args, void *argp) {
    (void)args;
    const int id = *(const int *)argp;
    const int n = __atomic_fetch_add(&ran_count, 1, __ATOMIC_SEQ_CST);
    if (n < 2)
        ran_order[n] = id;
    return 0;
}

static int status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

static void pair_case(const char *name, int r1_priority, int r2_priority) {
    ran_count = 0;
    ran_order[0] = ran_order[1] = 0;
    start_gate(0);
    start_gate(1);
    int id1 = 1, id2 = 2;
    const SceUID r1 = sceKernelCreateThread("r1", r_entry, r1_priority, 0x4000, 0,
                                            SCE_KERNEL_CPU_MASK_USER_1, NULL);
    const SceUID r2 = sceKernelCreateThread("r2", r_entry, r2_priority, 0x4000, 0, BOTH_CORES,
                                            NULL);
    sceKernelStartThread(r1, sizeof(id1), &id1);
    sceKernelStartThread(r2, sizeof(id2), &id2);
    // Both are READY behind the gates, and stay so, so their status can be read
    TEST_WAIT_FOR(status_of(r1) == STATUS_READY && status_of(r2) == STATUS_READY);
    const int status1 = status_of(r1), status2 = status_of(r2);

    release_gate(0);
    TEST_WAIT_FOR(ran_count >= 1);
    // A wrong pick would run R1 on core 0 within this time. The right one never does.
    sceKernelDelayThread(300000);
    const int first_r2_only = ran_count == 1 && ran_order[0] == 2;

    release_gate(1);
    TEST_WAIT_FOR(ran_count >= 2);
    join_or_timeout(r1);
    join_or_timeout(r2);
    const int second_r1 = ran_count == 2 && ran_order[1] == 1;

    PRINTF(status1, "%s: R1's status before the first release", name);
    PRINTF(status2, "%s: R2's status before the first release", name);
    PRINTF(first_r2_only, "%s: after core 0 is released, R2 ran and R1 did not", name);
    PRINTF(second_r1, "%s: after core 1 is released, R1 ran", name);
    PRINTF((ran_order[0] << 4) | ran_order[1], "%s: the order the threads ran in", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // Above the test threads, so the main thread runs on core 2 ahead of them
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);

    pair_case("R1 and R2 at 0xA0", 0xA0, 0xA0);
    pair_case("R1 at 0x90, R2 at 0xA0", 0x90, 0xA0);

    test_exit();
    return 0;
}
