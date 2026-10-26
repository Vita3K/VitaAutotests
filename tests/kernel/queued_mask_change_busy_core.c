// Whether a thread queued on core 0 runs on core 1 when its mask is narrowed to core 1 alone,
// where another thread spins at a lower, an equal or a higher priority. A gate thread G of the
// highest priority spins on core 0 without syscalls until the main thread lifts it. T is started
// from core 2 with mask core 0, so it waits queued behind G: STANDBY at 0x70, and READY at 0xA0
// in the queue the cores share. The spinner H spins on core 1 without syscalls until it is
// stopped, at 0xB0 (lower than T), at T's priority (equal) and at 0x50 (higher). The main thread,
// on core 2, then narrows T's mask to core 1. Each case prints the change's result and, 300 ms
// after it, T's status and whether T ran while G still holds core 0. H is then stopped, and 300
// ms later whether T ran again and the core it ran on are noted. G is lifted last, and a lower H
// is preempted by T at once. Cases: T at 0x70 and at 0xA0, each with H at 0x50, at T's priority
// and at 0xB0.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_PRIORITY SCE_KERNEL_HIGHEST_PRIORITY_USER
#define WINDOW_US 300000
#define SPINS 0xFFFFFFFFu

static SceUID gate;
static volatile int gate_up, gate_open, h_up, h_stop, t_ran, t_core;

// G, which spins on core 0 until the main thread lifts it
static int gate_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    gate_up = 1;
    for (volatile unsigned int n = 0; n < SPINS && !gate_open; n++)
        ;
    return 0;
}

// H, which spins on core 1 until it is stopped
static int spinner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    h_up = 1;
    for (volatile unsigned int n = 0; n < SPINS && !h_stop; n++)
        ;
    return 0;
}

// T, which notes that it ran and the core it ran on
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    t_core = info.currentCpuId;
    t_ran = 1;
    return 0;
}

// Returns whether thread is READY or STANDBY, that is queued behind the thread that runs
static int queued(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status == SCE_THREAD_READY || info.status == SCE_THREAD_STANDBY;
}

// Waits for a thread to end, for at most TEST_WAIT_US, and deletes it. A thread that doesn't end in
// time is left as it is, so a wrong result can't hang the run.
static void join_timed(SceUID thread) {
    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelWaitThreadEnd(thread, NULL, &timeout) >= 0)
        sceKernelDeleteThread(thread);
}

// Runs one case, with T at t_priority and the spinner at spinner_priority
static void run_case(int t_priority, int spinner_priority) {
    gate_up = gate_open = h_up = h_stop = t_ran = 0;
    t_core = -2;
    gate = sceKernelCreateThread("gate", gate_entry, GATE_PRIORITY, 0x4000, 0,
                                 SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(gate, 0, NULL);
    TEST_WAIT_FOR(gate_up);
    const SceUID t = sceKernelCreateThread("t", t_entry, t_priority, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(queued(t));
    const SceUID h = sceKernelCreateThread("h", spinner_entry, spinner_priority, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(h, 0, NULL);
    TEST_WAIT_FOR(h_up);

    const int result = sceKernelChangeThreadCpuAffinityMask(t, SCE_KERNEL_CPU_MASK_USER_1);
    sceKernelDelayThread(WINDOW_US);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(t, &info);
    const SceUInt32 status = info.status;
    const int ran_early = t_ran;

    h_stop = 1;
    join_timed(h);
    sceKernelDelayThread(WINDOW_US);
    const int ran_late = t_ran;

    gate_open = 1;
    join_timed(gate);
    join_timed(t);
    const int core = t_core;

    PRINTF(result, "T at 0x%X, spinner at 0x%X: the change to core 1", t_priority,
           spinner_priority);
    PRINTF(status, "T at 0x%X, spinner at 0x%X: T's status 300 ms after the change", t_priority,
           spinner_priority);
    PRINTF(ran_early, "T at 0x%X, spinner at 0x%X: T ran within 300 ms, G still holding core 0",
           t_priority, spinner_priority);
    PRINTF(ran_late, "T at 0x%X, spinner at 0x%X: T ran within 300 ms after the spinner stopped",
           t_priority, spinner_priority);
    PRINTF(core, "T at 0x%X, spinner at 0x%X: the core T ran on", t_priority, spinner_priority);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);
    static const int t_priorities[2] = { 0x70, 0xA0 };
    for (int t = 0; t < 2; t++) {
        const int t_priority = t_priorities[t];
        // The spinner is lower than T, equal to it, or higher
        const int spinner_priorities[3] = { 0xB0, t_priority, 0x50 };
        for (int s = 0; s < 3; s++)
            run_case(t_priority, spinner_priorities[s]);
    }
    test_exit();
    return 0;
}
