// Thread event handler registrations for a started thread that can't run yet, since a gate thread
// spins on its core. Case a registers a START and END handler for the thread before it is started.
// Case b registers the same after the start, while the thread is still ready behind the gate. Each
// case lifts the gate, joins the thread and prints whether the START run happened, whether the END
// run happened, and whether START ran before the thread's entry took its first step.
// The main thread runs on core 1. The gate is a high priority spinner on core 0 with no syscalls,
// and the test thread is on core 0 too, so it stays ready until the gate is lifted.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_PRIORITY SCE_KERNEL_HIGHEST_PRIORITY_USER
#define T_PRIORITY 0xA0
#define SPINS 400000000

static SceUID gate, target;
static volatile int gate_up, gate_open;
static volatile int seq, start_seq, entry_seq, start_runs, end_runs;

static int gate_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    gate_up = 1;
    for (volatile int n = 0; n < SPINS && !gate_open; n++)
        ;
    return 0;
}

static void gate_close(void) {
    gate_up = gate_open = 0;
    gate = sceKernelCreateThread("gate", gate_entry, GATE_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(gate, 0, NULL);
    TEST_WAIT_FOR(gate_up);
}

static void gate_lift(void) {
    gate_open = 1;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(gate, NULL, &timeout);
    sceKernelDeleteThread(gate);
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    entry_seq = ++seq;
    return 0;
}

// Counts the START and END runs of the target, and records when START ran
static int event_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)thread_id;
    (void)arg;
    (void)common;
    if (type == SCE_KERNEL_THREAD_EVENT_TYPE_START) {
        start_runs++;
        start_seq = ++seq;
    } else {
        end_runs++;
    }
    return 0;
}

static SceUID register_handler(void) {
    return sceKernelRegisterThreadEventHandler("handler", target, SCE_KERNEL_THREAD_EVENT_TYPE_START | SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, event_handler, NULL);
}

// Runs one case. before_start registers the handler before the start, otherwise after it.
static void run_case(const char *name, int before_start) {
    start_runs = end_runs = seq = start_seq = entry_seq = 0;
    gate_close();
    target = sceKernelCreateThread("target", target_entry, T_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    SceUID id = -1;
    if (before_start)
        id = register_handler();
    sceKernelStartThread(target, 0, NULL);
    if (!before_start)
        id = register_handler();
    gate_lift();

    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelWaitThreadEnd(target, NULL, &timeout), "%s: the wait for the end", name);
    PRINTF(id < 0 ? id : 1, "%s: register", name);
    PRINTF(start_runs > 0, "%s: START ran", name);
    PRINTF(end_runs > 0, "%s: END ran", name);
    PRINTF(start_runs > 0 && start_seq < entry_seq, "%s: START ran before the entry's first step", name);
    if (id >= 0)
        PRINTF(sceKernelUnregisterThreadEventHandler(id), "%s: unregister", name);
    sceKernelDeleteThread(target);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("a: registered before the start", 1);
    run_case("b: registered after the start", 0);

    test_exit();
    return 0;
}
