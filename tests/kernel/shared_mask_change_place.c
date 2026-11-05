// Whether a mask change of a thread READY in the queue that cores 0 and 1 share keeps its place
// in the queue. Two gate threads at 0x40 spin without syscalls, one on core 0 and one on core 1.
// The main thread, on core 2, starts A and B at 0xA0 with the mask of cores 0 and 1, in that
// order, so both wait READY behind the gates, A first. Then A's mask is set to the value it
// already has, from core 2, which is outside that mask, or from the gate of core 0, which is
// inside it. Each case prints the status and the current CPU of A and B before the gate of core 0
// ends, which of them runs first once it has, and the core that one ran on. The gate of core 1
// holds on until both ran.
//
// On a PS Vita TV on firmware 3.65 both threads are READY with the current CPU 0x7FFF before the
// gate of core 0 ends, and none has run. After a change from the gate of core 0, A runs first.
// After a change from core 2, B runs first, as if A had moved behind it. In both cases the first
// to run does so on core 0, and the change returns the old mask 0x30000.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_PRIORITY 0x40
#define MAIN_PRIORITY 0x60
#define TWO_CORES (SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_1)
#define SPINS 400000000

static volatile int gate_up[2], stop_gate[2];
static volatile int change_now, change_done, change_result;
static volatile int ran_count;
static int ran_order[2], ran_cpu[2];
static SceUID gates[2], thread_a;

// Gate i spins on core i until told to stop. Gate 0 also changes A's mask when told to.
static int gate_entry(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    gate_up[i] = 1;
    for (volatile int n = 0; n < SPINS && !stop_gate[i]; n++) {
        if (i == 0 && change_now && !change_done) {
            change_result = sceKernelChangeThreadCpuAffinityMask(thread_a, TWO_CORES);
            change_done = 1;
        }
    }
    return 0;
}

// Records its id, 1 for A and 2 for B, and the core it runs on, in the order the threads run
static int ab_entry(SceSize args, void *argp) {
    (void)args;
    const int id = *(const int *)argp;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    const int n = __atomic_fetch_add(&ran_count, 1, __ATOMIC_SEQ_CST);
    if (n < 2) {
        ran_order[n] = id;
        ran_cpu[n] = info.currentCpuId;
    }
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

static void run_case(const char *name, int from_gate) {
    ran_count = 0;
    ran_order[0] = ran_order[1] = 0;
    ran_cpu[0] = ran_cpu[1] = -2;
    change_now = change_done = 0;
    change_result = -1;
    start_gate(0);
    start_gate(1);
    int id_a = 1, id_b = 2;
    thread_a = sceKernelCreateThread("a", ab_entry, 0xA0, 0x4000, 0, TWO_CORES, NULL);
    const SceUID thread_b = sceKernelCreateThread("b", ab_entry, 0xA0, 0x4000, 0, TWO_CORES, NULL);
    sceKernelStartThread(thread_a, sizeof(id_a), &id_a);
    sceKernelStartThread(thread_b, sizeof(id_b), &id_b);

    if (from_gate) {
        change_now = 1;
        TEST_WAIT_FOR(change_done);
    } else {
        change_result = sceKernelChangeThreadCpuAffinityMask(thread_a, TWO_CORES);
    }

    SceKernelThreadInfo info_a = { .size = sizeof(info_a) };
    SceKernelThreadInfo info_b = { .size = sizeof(info_b) };
    sceKernelGetThreadInfo(thread_a, &info_a);
    sceKernelGetThreadInfo(thread_b, &info_b);
    const int ran_before = ran_count;

    stop_gate[0] = 1;
    join_or_timeout(gates[0]);
    TEST_WAIT_FOR(ran_count >= 2);
    stop_gate[1] = 1;
    join_or_timeout(gates[1]);
    join_or_timeout(thread_a);
    join_or_timeout(thread_b);

    PRINTF(change_result, "%s: the change", name);
    PRINTF(info_a.status, "%s: A's status before the gate ends", name);
    PRINTF(info_b.status, "%s: B's status before the gate ends", name);
    PRINTF(info_a.currentCpuId, "%s: A's current CPU before the gate ends", name);
    PRINTF(info_b.currentCpuId, "%s: B's current CPU before the gate ends", name);
    PRINTF(ran_before, "%s: threads that ran before the gate ends", name);
    PRINTF(ran_order[0], "%s: the first to run, 1 for A and 2 for B", name);
    PRINTF(ran_cpu[0], "%s: the core it ran on", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // Above the test threads, so the main thread runs on core 2 ahead of them
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("change from core 2, outside the mask", 0);
    run_case("change from the gate of core 0, inside the mask", 1);

    test_exit();
    return 0;
}
