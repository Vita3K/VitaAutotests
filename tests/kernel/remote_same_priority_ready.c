// What a priority change from another core does to a READY thread at the priority it has. T and U
// run at 0x70 on core 0, behind a gate thread of the highest priority that spins there without
// syscalls until the main thread, on core 1, lifts it. The gate makes T READY by a signal, from
// core 0, in all cases. U is either made READY by a signal after T, or started by the main thread
// from core 1, which makes it STANDBY (wake_core_origin). Then the main thread sets T's priority
// to the 0x70 it has, or does nothing. Cases:
// - T and U READY, no change
// - T and U READY, T set to 0x70
// - T READY, U STANDBY, no change
// - T READY, U STANDBY, T set to 0x70
// Each case prints what the change returns, both statuses just before the gate lifts, and which of
// the two runs first after it. local_ready_requeue makes the same change from core 0.
//
// On a PS Vita TV on firmware 3.65 the change returns 0 and the statuses just before the gate
// lifts are READY 0x2 for T, and READY 0x2 or STANDBY 0x4 for U, the same with and without the
// change. With U READY, T runs first without the change and U runs first after it. With U
// STANDBY, T runs first either way.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_PRIORITY SCE_KERNEL_HIGHEST_PRIORITY_USER
#define PRIORITY 0x70
#define SPINS 400000000

enum Action { NONE, SIGNAL_T, SIGNAL_U };

static SceUID sema[2], gate;
static volatile int gate_up, gate_open, gate_action, gate_acted, ran;
static volatile int order[2];

static int gate_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    gate_up = 1;
    for (volatile int n = 0; n < SPINS && !gate_open; n++) {
        const int action = gate_action;
        if (action != NONE && gate_acted != action) {
            sceKernelSignalSema(sema[action - 1], 1);
            gate_acted = action;
        }
    }
    return 0;
}

// Has the gate signal one of the semaphores from core 0 and waits until it did
static void gate_signal(int action) {
    gate_action = action;
    TEST_WAIT_FOR(gate_acted == action);
}

static void record(int who) {
    order[__atomic_fetch_add(&ran, 1, __ATOMIC_SEQ_CST)] = who;
}

struct Arg {
    int id, wait;
};

// A thread that waits for its semaphore first when told to, then records that it ran
static int thread_entry(SceSize args, void *argp) {
    (void)args;
    const struct Arg arg = *(const struct Arg *)argp;
    if (arg.wait) {
        SceUInt32 timeout = 2 * TEST_WAIT_US;
        sceKernelWaitSema(sema[arg.id - 1], 1, &timeout);
    }
    record(arg.id);
    return 0;
}

static void start(SceUID thread, int id, int wait) {
    struct Arg arg = { id, wait };
    sceKernelStartThread(thread, sizeof(arg), &arg);
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

static void run_case(const char *name, int u_ready, int change) {
    ran = 0;
    order[0] = order[1] = 0;
    sema[0] = sceKernelCreateSema("t", 0, 0, 1, NULL);
    sema[1] = sceKernelCreateSema("u", 0, 0, 1, NULL);
    const SceUID t = sceKernelCreateThread("t", thread_entry, PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID u = sceKernelCreateThread("u", thread_entry, PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    start(t, 1, 1);
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, sema[0]));
    if (u_ready) {
        start(u, 2, 1);
        TEST_WAIT_FOR(test_in_wait(u, WAIT_TYPE_SEMA, sema[1]));
    }
    gate_up = gate_open = 0;
    gate_action = gate_acted = NONE;
    gate = sceKernelCreateThread("gate", gate_entry, GATE_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(gate, 0, NULL);
    TEST_WAIT_FOR(gate_up);
    gate_signal(SIGNAL_T);
    if (u_ready) {
        gate_signal(SIGNAL_U);
    } else {
        start(u, 2, 0);
        // The start hands U over to core 0, which the gate keeps
        sceKernelDelayThread(10000);
    }
    int result = 0;
    if (change) {
        result = sceKernelChangeThreadPriority(t, PRIORITY);
        // The change runs on core 0 as a request, so give it time to get there
        sceKernelDelayThread(50000);
    }
    const SceUInt32 t_status = status_of(t), u_status = status_of(u);
    gate_open = 1;
    sceKernelWaitThreadEnd(gate, NULL, NULL);
    sceKernelDeleteThread(gate);
    TEST_WAIT_FOR(ran == 2);
    // Joined first, so every record the threads made is complete
    test_join_thread(t);
    test_join_thread(u);
    sceKernelDeleteSema(sema[0]);
    sceKernelDeleteSema(sema[1]);

    if (change)
        PRINTF(result, "%s: the change", name);
    PRINTF(t_status, "%s: T's status before the gate lifts", name);
    PRINTF(u_status, "%s: U's status before the gate lifts", name);
    PRINTF(order[0], "%s: the first to run, 1 for T and 2 for U", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    run_case("T and U READY, no change", 1, 0);
    run_case("T and U READY, T set to 0x70", 1, 1);
    run_case("T READY and U STANDBY, no change", 0, 0);
    run_case("T READY and U STANDBY, T set to 0x70", 0, 1);
    test_exit();
    return 0;
}
