// Which core a thread goes to when a thread of core 2 takes core 2 from it and its mask also holds
// cores 0 and 1, which both run a spinner. T, at 0x70 or 0xA0 with the mask of cores 0 to 2, wakes
// on core 2, where the main thread runs at a lower priority than anything else. T starts B, at
// 0x48 and pinned to core 2, which takes core 2 from T before the call returns, and ends at once.
// Spinners on cores 0 and 1 count in loops without syscalls, and so does T once it runs again. The
// main thread waits for T to run again, 10 ms later it notes T's status and current CPU, then
// whether T and the spinners counted on over 50 ms. A case whose T didn't wake on core 2 is run
// again, up to 5 times. The cases are two spinners at 0xB0 with T at 0x70, the same with T at 0xA0
// (only that T took the place of one spinner is printed, since which core takes a thread of the
// queue the cores share varies), the spinner of core 1 at 0xB8 and the one of core 0 at 0xB0, and
// the other way round, with T at 0x70.
//
// On a PS Vita TV on firmware 3.65 T is RUNNING 10 ms after B ended and counts on in every case.
// At 0x70, T runs on core 0 when both spinners are at 0xB0, and on the core of the 0xB8 spinner
// when one is. The spinner on the core T runs on stops counting and the other one counts on. At
// 0xA0, T runs on core 0 or 1 and one of the spinners counts on.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define ALL_CORES (SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2)
#define SPINS 400000000
#define MAIN_PRIORITY 0xBC

// The counters of the spinners of cores 0 and 1, and of T
static volatile unsigned int counts[3];
static volatile int stop, t_core_2, t_started_b, t_failed;
static SceUID sema, b_thread;

static int count_entry(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    for (int n = 0; n < SPINS && !stop; n++)
        counts[i]++;
    return 0;
}

static SceUID start_spinner(int core, int priority) {
    const SceUID thread = sceKernelCreateThread("spinner", count_entry, priority, 0x4000, 0,
                                                SCE_KERNEL_CPU_MASK_USER_0 << core, NULL);
    sceKernelStartThread(thread, sizeof(core), &core);
    TEST_WAIT_FOR(counts[core] != 0);
    return thread;
}

static int b_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    if (info.currentCpuId != 2) {
        t_failed = 1;
        return 0;
    }
    t_core_2 = 1;
    sceKernelStartThread(b_thread, 0, NULL);
    t_started_b = 1;
    for (int n = 0; n < SPINS && !stop; n++)
        counts[2]++;
    return 0;
}

// Returns whether T woke on core 2, and the results if it did
static int run_attempt(int t_priority, int core_0_priority, int core_1_priority, int *status,
                       int *cpu, int *counted) {
    stop = t_core_2 = t_started_b = t_failed = 0;
    for (int i = 0; i < 3; i++)
        counts[i] = 0;
    const SceUID s0 = start_spinner(0, core_0_priority);
    const SceUID s1 = start_spinner(1, core_1_priority);
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    b_thread = sceKernelCreateThread("b", b_entry, 0x48, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_2, NULL);
    const SceUID t = sceKernelCreateThread("t", t_entry, t_priority, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_2, NULL);
    sceKernelStartThread(t, 0, NULL);
    // T waits on core 2, its last core, then gets the mask of three cores and is woken there
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, sema));
    sceKernelChangeThreadCpuAffinityMask(t, ALL_CORES);
    sceKernelSignalSema(sema, 1);
    TEST_WAIT_FOR(t_started_b || t_failed);
    if (t_started_b) {
        // T has run again on the core it was given
        sceKernelDelayThread(10000);
        SceKernelThreadInfo info = { .size = sizeof(info) };
        sceKernelGetThreadInfo(t, &info);
        *status = info.status;
        *cpu = info.currentCpuId;
        const unsigned int before[3] = { counts[0], counts[1], counts[2] };
        sceKernelDelayThread(50000);
        for (int i = 0; i < 3; i++)
            counted[i] = counts[i] != before[i];
    }
    stop = 1;
    if (t_started_b)
        sceKernelWaitThreadEnd(b_thread, NULL, NULL);
    sceKernelDeleteThread(b_thread);
    test_join_thread(t);
    test_join_thread(s0);
    test_join_thread(s1);
    sceKernelDeleteSema(sema);
    return t_started_b;
}

static void run_case(int t_priority, int core_0_priority, int core_1_priority, int exact) {
    int status = 0, cpu = 0, counted[3] = { 0, 0, 0 };
    int ok = 0;
    for (int attempt = 0; attempt < 5 && !ok; attempt++)
        ok = run_attempt(t_priority, core_0_priority, core_1_priority, &status, &cpu, counted);

    char name[96];
    sceClibSnprintf(name, sizeof(name), "T at 0x%X, core 0 at 0x%X, core 1 at 0x%X", t_priority,
                    core_0_priority, core_1_priority);
    PRINTF(ok, "%s: T woke on core 2 and B took it", name);
    if (!ok)
        return;
    PRINTF(status, "%s: T's status once B ended", name);
    PRINTF(counted[2], "%s: T counted on", name);
    if (exact) {
        PRINTF(cpu, "%s: T's current CPU", name);
        PRINTF(counted[0], "%s: the spinner of core 0 counted on", name);
        PRINTF(counted[1], "%s: the spinner of core 1 counted on", name);
    } else {
        PRINTF(cpu >= 0 && cpu <= 1, "%s: T runs on core 0 or 1", name);
        PRINTF(counted[0] + counted[1], "%s: how many spinners counted on", name);
    }
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);
    run_case(0x70, 0xB0, 0xB0, 1);
    run_case(0xA0, 0xB0, 0xB0, 0);
    run_case(0x70, 0xB0, 0xB8, 1);
    run_case(0x70, 0xB8, 0xB0, 1);
    test_exit();
    return 0;
}
