// Whether a priority change from another core gives a thread waiting for a running thread's core
// that core before the change returns, or only later. L spins on core 1 without syscalls, a
// spinner thread at 0x40 holds core 2 the same way, and the main thread runs on core 0 at 0x50. R,
// of a lower priority than L, is started from core 0, so it waits for a core of its mask. 10 ms
// later the main thread notes R's status and current CPU, then either lowers L below R or raises
// R above L. It notes whether R ran before the change
// returned, read right after it, then whether R ran within 10 ms and L's status then. The cases:
// - L at 0x70, R at 0x78, in the queues of one core, with L lowered to 0x7C or R raised to 0x6C
// - L at 0xA0, R at 0xA8, in the queue the cores share, with L lowered to 0xAC or R raised to 0x9C
// Each runs with R on core 1 alone and on cores 1 and 2, and prints those and the core R ran on.
// The read right after the change tells apart a change that waits for core 1 to switch from one
// that only asks it to. remote_priority_change has the same changes with R on L's core alone,
// made from the core R was started from.
//
// On a PS Vita TV on firmware 3.65 no change lets R run before the call returns. At 0x70 R waits
// STANDBY on its core. Lowering the running L below R doesn't let R run within 10 ms, so a STANDBY
// thread doesn't count until its core switches, and R runs on its core later. Raising R above L
// lets it run within 10 ms when it waits on L's core, but not when it waits STANDBY on core 2,
// which the spinner at 0x40 holds. At 0xA0 R is READY in the queue the cores share, and either
// change lets it take core 1 within 10 ms. L reports RUNNING right after each change.
//
// At 0x70 on cores 1 and 2, the start puts R in the queue of core 1 in some runs and of core 2 in
// others, so those cases print what holds either way: R runs on the core it waited on, and raising
// R lets it run within 10 ms exactly when it waited on L's core.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x50
#define SPINS 400000000

static volatile int l_up, l_stop, z_up, z_stop, r_ran, r_cpu;

static int l_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    l_up = 1;
    for (volatile int n = 0; n < SPINS && !l_stop; n++)
        ;
    return 0;
}

static int z_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    z_up = 1;
    for (volatile int n = 0; n < SPINS && !z_stop; n++)
        ;
    return 0;
}

static int r_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    r_ran = 1;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    r_cpu = info.currentCpuId;
    return 0;
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

static void run_case(int l_priority, SceUInt32 r_mask, int lower_l) {
    l_up = l_stop = z_up = z_stop = r_ran = 0;
    r_cpu = -1;
    const int r_priority = l_priority + 8;
    const SceUID z = sceKernelCreateThread("z", z_entry, SCE_KERNEL_HIGHEST_PRIORITY_USER, 0x4000,
                                           0, SCE_KERNEL_CPU_MASK_USER_2, NULL);
    sceKernelStartThread(z, 0, NULL);
    TEST_WAIT_FOR(z_up);
    const SceUID l = sceKernelCreateThread("l", l_entry, l_priority, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(l, 0, NULL);
    TEST_WAIT_FOR(l_up);
    const SceUID r = sceKernelCreateThread("r", r_entry, r_priority, 0x4000, 0, r_mask, NULL);
    sceKernelStartThread(r, 0, NULL);
    // The start hands R over to a core L or the spinner keeps
    sceKernelDelayThread(10000);
    SceKernelThreadInfo before = { .size = sizeof(before) };
    sceKernelGetThreadInfo(r, &before);
    const int ran_before = r_ran;

    const int result = lower_l ? sceKernelChangeThreadPriority(l, r_priority + 4)
                               : sceKernelChangeThreadPriority(r, l_priority - 4);
    const int ran_in_call = r_ran;
    sceKernelDelayThread(10000);
    const int ran_after = r_ran;
    const SceUInt32 l_status = status_of(l);

    l_stop = 1;
    test_join_thread(l);
    z_stop = 1;
    test_join_thread(z);
    TEST_WAIT_FOR(r_ran);
    // An R left in a queue that no core takes it from gets every core
    const int rescued = !r_ran;
    if (rescued) {
        sceKernelChangeThreadCpuAffinityMask(r, SCE_KERNEL_CPU_MASK_USER_ALL);
        TEST_WAIT_FOR(r_ran);
    }
    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelWaitThreadEnd(r, NULL, &timeout) >= 0)
        sceKernelDeleteThread(r);

    char name[96];
    sceClibSnprintf(name, sizeof(name), "L at 0x%X, R on %s, %s", l_priority,
                    r_mask == SCE_KERNEL_CPU_MASK_USER_1 ? "core 1" : "cores 1 and 2",
                    lower_l ? "L lowered below R" : "R raised above L");
    // Which of its cores' queues R waits in at 0x70 on cores 1 and 2 varies between runs
    const int either_queue = l_priority == 0x70 && r_mask != SCE_KERNEL_CPU_MASK_USER_1;
    PRINTF(ran_before, "%s: R ran before the change", name);
    PRINTF(before.status, "%s: R's status before the change", name);
    if (either_queue)
        PRINTF(before.currentCpuId == 1 || before.currentCpuId == 2, "%s: R's current CPU before the change is 1 or 2", name);
    else
        PRINTF(before.currentCpuId, "%s: R's current CPU before the change", name);
    PRINTF(result, "%s: the change", name);
    PRINTF(ran_in_call, "%s: R ran before the change returned", name);
    if (either_queue && !lower_l)
        PRINTF(ran_after == (before.currentCpuId == 1), "%s: R ran within 10 ms of the change exactly when it waited on L's core", name);
    else
        PRINTF(ran_after, "%s: R ran within 10 ms of the change", name);
    PRINTF(l_status, "%s: L's status then", name);
    PRINTF(rescued, "%s: R only ran once given every core", name);
    if (either_queue)
        PRINTF(r_cpu == before.currentCpuId, "%s: R ran on the core it waited on", name);
    else
        PRINTF(r_cpu, "%s: the core R ran on", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);
    static const int priorities[2] = { 0x70, 0xA0 };
    static const SceUInt32 masks[2] = {
        SCE_KERNEL_CPU_MASK_USER_1,
        SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2,
    };
    for (int p = 0; p < 2; p++)
        for (int m = 0; m < 2; m++)
            for (int lower_l = 1; lower_l >= 0; lower_l--)
                run_case(priorities[p], masks[m], lower_l);
    test_exit();
    return 0;
}
