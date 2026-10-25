// Whether a thread that may run on cores 0 and 1 and last ran on core 0 waits for core 0 once it
// is ready, or runs on core 1 when only that one frees up, depending on what made it ready. Gate
// threads of the highest priority spin on both cores without syscalls, and the main thread runs
// on core 2. T starts while core 1 is busy, so it runs on core 0 and waits there with a 100 ms
// timeout before core 0's gate closes. Then its timeout passes or the main thread signals it,
// the gate of core 1 lifts and the main thread waits 50 ms. Each case prints whether T was ready
// before the lift, and whether it ran on core 1 before core 0's gate lifted. The cases run at 0x70,
// where a start or a signal makes a thread STANDBY in the queue of one core (wake_ready_areas), and
// at 0xA0, where all of them make it READY.
//
// On a PS Vita TV on firmware 3.65, at 0x70 T waits for core 0 either way. At 0xA0 it runs on core
// 1 at once either way. After its timeout at 0x70, T is READY in some runs and STANDBY in others,
// and once both gates lifted it runs on core 0 in some runs and core 1 in others, so the test
// doesn't print either.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_PRIORITY SCE_KERNEL_HIGHEST_PRIORITY_USER
#define SPINS 400000000

static SceUID sema;
static volatile int gate_up[2], gate_open[2], t_ran, t_cpu;
static SceUID gates[2];

static int gate_entry(SceSize args, void *argp) {
    (void)args;
    const int core = *(const int *)argp;
    gate_up[core] = 1;
    for (volatile int n = 0; n < SPINS && !gate_open[core]; n++)
        ;
    return 0;
}

static void gate_close(int core) {
    gate_up[core] = gate_open[core] = 0;
    gates[core] = sceKernelCreateThread("gate", gate_entry, GATE_PRIORITY, 0x4000, 0, core ? SCE_KERNEL_CPU_MASK_USER_1 : SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(gates[core], sizeof(core), &core);
    TEST_WAIT_FOR(gate_up[core]);
}

static void gate_lift(int core) {
    gate_open[core] = 1;
    sceKernelWaitThreadEnd(gates[core], NULL, NULL);
    sceKernelDeleteThread(gates[core]);
}

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 100000;
    sceKernelWaitSema(sema, 1, &timeout);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    t_cpu = info.currentCpuId;
    t_ran = 1;
    return 0;
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

static void run_case(int timeout, int priority) {
    t_ran = 0;
    t_cpu = -2;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    gate_close(1);
    const SceUID t = sceKernelCreateThread("t", t_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(t, 0, NULL);
    test_wait_until_waiting(t);
    gate_close(0);
    if (timeout)
        sceKernelDelayThread(300000);
    else
        sceKernelSignalSema(sema, 1);
    sceKernelDelayThread(10000);

    const SceUInt32 status = status_of(t);
    gate_lift(1);
    sceKernelDelayThread(50000);
    const int ran_on_core_1 = t_ran;
    gate_lift(0);
    TEST_WAIT_FOR(t_ran);
    // Joined first, so every record the threads made is complete
    test_join_thread(t);
    const char *const name = timeout ? "timeout" : "signal";
    PRINTF(status == SCE_THREAD_READY || status == SCE_THREAD_STANDBY, "%s, priority 0x%X: T was READY or STANDBY before a gate lifted", name, priority);
    PRINTF(ran_on_core_1 && t_cpu == 1, "%s, priority 0x%X: T ran on core 1 while core 0 was still busy", name, priority);
    sceKernelDeleteSema(sema);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);
    static const int priorities[2] = { 0x70, 0xA0 };
    for (int p = 0; p < 2; p++) {
        run_case(1, priorities[p]);
        run_case(0, priorities[p]);
    }
    test_exit();
    return 0;
}
