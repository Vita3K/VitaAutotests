// Whether a VM thread that is queued on core 0 behind a gate, then suspended,
// has its CPU mask moved to core 1 and is resumed, runs on core 1 while the
// gate still holds core 0. A gate G at 0x40 spins on core 0 without syscalls
// until the main thread lifts it. T, a VM thread on core 0 at 0x70, and a
// second case at 0xA0, becomes ready behind G. The main thread runs on core 2,
// so core 1 stays idle. Each case has two orders of the calls:
// - suspended, its mask moved to core 1, then resumed
// - the control: suspended, resumed, then its mask moved to core 1
// Each case prints the results of its three calls, T's status once the mask has
// moved, whether T ran within 250 ms while G still held core 0, and the core T
// ran on. In the control, core 1 may already have picked T when its status is
// read, so the control prints only whether T was still suspended then. The gate
// is lifted only after those reads, and T is joined after it has run.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define GATE_PRIORITY 0x40
#define READY 0x2
#define STANDBY 0x4
#define SUSPENDED 0x100

static SceUID gate;
static volatile int gate_up, gate_open, t_ran, t_cpu;

// Spins on core 0 until the gate is lifted
static int gate_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    gate_up = 1;
    while (!gate_open)
        ;
    return 0;
}

// Records the core it runs on
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceKernelThreadInfo info = {.size = sizeof(info)};
    sceKernelGetThreadInfo(0, &info);
    t_cpu = info.currentCpuId;
    t_ran = 1;
    return 0;
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = {.size = sizeof(info)};
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

// Whether thread is ready to run on its core, STANDBY or READY
static int queued(SceUID thread) {
    const SceUInt32 status = status_of(thread);
    return status == READY || status == STANDBY;
}

static void run_case(int priority, int control) {
    const char *name =
        control ? "control, mask moved after the resume" : "mask moved while suspended";
    gate_up = gate_open = t_ran = 0;
    t_cpu = -2;
    gate = sceKernelCreateThread("gate", gate_entry, GATE_PRIORITY, 0x4000, 0,
                                 SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(gate, 0, NULL);
    TEST_WAIT_FOR(gate_up);
    const SceUID t = sceKernelCreateThread("t", t_entry, priority, 0x4000, THREAD_ATTR_VM,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(queued(t));
    int suspended, resumed, moved;
    SceUInt32 status;
    if (control) {
        suspended = sceKernelSuspendThreadForVM(t);
        resumed = sceKernelResumeThreadForVM(t);
        moved = sceKernelChangeThreadCpuAffinityMask(t, SCE_KERNEL_CPU_MASK_USER_1);
        status = status_of(t);
    } else {
        suspended = sceKernelSuspendThreadForVM(t);
        moved = sceKernelChangeThreadCpuAffinityMask(t, SCE_KERNEL_CPU_MASK_USER_1);
        status = status_of(t);
        resumed = sceKernelResumeThreadForVM(t);
    }
    sceKernelDelayThread(250000);
    const int ran_early = t_ran;
    gate_open = 1;
    sceKernelWaitThreadEnd(gate, NULL, NULL);
    sceKernelDeleteThread(gate);
    TEST_WAIT_FOR(t_ran);
    test_join_thread(t);
    PRINTF(suspended, "%s at 0x%X: the suspend", name, priority);
    PRINTF(moved, "%s at 0x%X: the mask change", name, priority);
    PRINTF(resumed, "%s at 0x%X: the resume", name, priority);
    if (control)
        PRINTF(status == SUSPENDED, "%s at 0x%X: it was suspended once the mask moved", name,
               priority);
    else
        PRINTF(status, "%s at 0x%X: its status once the mask moved", name, priority);
    PRINTF(ran_early, "%s at 0x%X: it ran within 250 ms, with the gate on core 0", name, priority);
    PRINTF(t_cpu, "%s at 0x%X: the core it ran on", name, priority);
}

int main(void) {
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);
    run_case(0x70, 0);
    run_case(0xA0, 0);
    run_case(0x70, 1);
    run_case(0xA0, 1);
    test_exit();
    return 0;
}
