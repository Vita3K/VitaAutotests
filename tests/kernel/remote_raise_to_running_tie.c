// Whether raising a thread that is ready behind a running thread to exactly the running thread's
// priority lets it run. L spins on core 0 without syscalls. When it starts, L creates R, masked to
// core 0, and starts it at a lower priority, so R is ready behind L and doesn't wait. The main
// thread, on core 1, then raises R to L's priority, waits 300 ms and notes whether R ran and L's
// status. Then it stops L, and R runs once L has ended. The step order prints one hex digit per
// step, in the order the steps happen: 1 when L starts, 2 when R runs, 3 when L ends. Cases:
// - L at 0x70, R at 0x78, with R raised to 0x70
// - L at 0xA0, R at 0xA8, with R raised to 0xA0
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define SPINS 400000000

static volatile int l_up, l_stop, r_ran;
static volatile SceUID r_uid;
static int r_prio;
static volatile int steps[4], step_count;

// Records the next step of the run as one digit
static void step(int digit) {
    const int n = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (n < 4)
        steps[n] = digit;
}

static int r_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    step(2);
    r_ran = 1;
    return 0;
}

static int l_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    step(1);
    const SceUID r = sceKernelCreateThread("r", r_entry, r_prio, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    r_uid = r;
    sceKernelStartThread(r, 0, NULL);
    l_up = 1;
    for (volatile int n = 0; n < SPINS && !l_stop; n++)
        ;
    step(3);
    return 0;
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

static void run_case(int l_priority, int r_start_priority) {
    l_up = l_stop = r_ran = 0;
    step_count = 0;
    r_prio = r_start_priority;
    const SceUID l = sceKernelCreateThread("l", l_entry, l_priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(l, 0, NULL);
    TEST_WAIT_FOR(l_up);
    const int result = sceKernelChangeThreadPriority(r_uid, l_priority);
    sceKernelDelayThread(300000);
    const int ran = r_ran;
    const SceUInt32 l_status = status_of(l);
    l_stop = 1;
    test_join_thread(l);
    // Once L has ended R is the only thread left on its core, so it runs
    TEST_WAIT_FOR(r_ran);
    test_join_thread(r_uid);
    int order = 0;
    for (int i = 0; i < step_count && i < 4; i++)
        order = order * 16 + steps[i];
    PRINTF(result, "L at 0x%X, R at 0x%X, R raised to L's priority: the change", l_priority, r_start_priority);
    PRINTF(ran, "L at 0x%X, R at 0x%X, R raised to L's priority: R ran within 300 ms of the change", l_priority, r_start_priority);
    PRINTF(l_status, "L at 0x%X, R at 0x%X, R raised to L's priority: L's status then", l_priority, r_start_priority);
    PRINTF(order, "L at 0x%X, R at 0x%X, R raised to L's priority: the step order after L is stopped", l_priority, r_start_priority);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    run_case(0x70, 0x78);
    run_case(0xA0, 0xA8);
    test_exit();
    return 0;
}
