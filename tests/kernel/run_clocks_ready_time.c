// The run clocks of a thread that is queued behind a running thread, READY or STANDBY, and of the
// thread that takes the core from it. L is a thread on core 0 at 0x70 that spins without syscalls
// until the test stops it. The main thread, on core 2, starts M on core 0 behind L, at 0xA0 or at
// 0x70, then starts T on core 0 at 0x60, which spins for half a second and so runs ahead of L.
// While T runs, the main thread reads the run clocks of L and M. Once T has ended it reads the
// clocks of T, L and M again, and M's status. Then it stops L, and M gets the core once L has
// ended. Each case prints whether L's clocks grew by less than 100 ms during T's run, whether T's
// clocks grew by more than 250 ms, whether M's clocks did not grow, and M's status once T has
// ended. The cases are M at 0xA0 and M at 0x70.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define RUNNING 0x1
// How long T spins, and the margins the comparisons use around the expected growth
#define SPIN_US 500000
#define GROWTH_US 250000
#define QUIET_US 100000

static SceUID l, m, t;
static volatile int l_stop;

// Spins on core 0 until the main thread stops it, without a call that waits
static int l_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    while (!l_stop)
        ;
    return 0;
}

// Busy waits for us microseconds, without a call that waits
static void spin(SceUInt64 us) {
    const SceInt64 start = sceKernelGetSystemTimeWide();
    while (sceKernelGetSystemTimeWide() - start < (SceInt64)us) {
    }
}

// Spins for SPIN_US, then ends
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    spin(SPIN_US);
    return 0;
}

// Ends at once, so it runs only once the core is free
static int m_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

// Reads the run clocks of thread into clocks, and returns the result of the info call
static int run_clocks(SceUID thread, SceUInt64 *clocks) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    const int result = sceKernelGetThreadInfo(thread, &info);
    *clocks = info.runClocks;
    return result;
}

// Waits for thread to end, for at most TEST_WAIT_US, then deletes it
static void join_bounded(SceUID thread) {
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);
}

// Runs one case, with M at m_priority
static void run_case(const char *name, int m_priority) {
    l_stop = 0;
    l = sceKernelCreateThread("l", l_entry, 0x70, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(l, 0, NULL);
    TEST_WAIT_FOR(status_of(l) == RUNNING);
    m = sceKernelCreateThread("m", m_entry, m_priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0,
        NULL);
    sceKernelStartThread(m, 0, NULL);
    t = sceKernelCreateThread("t", t_entry, 0x60, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    SceUInt64 t_before = 0, t_after = 0, l_before = 0, l_after = 0, m_before = 0, m_after = 0;
    run_clocks(t, &t_before);
    sceKernelStartThread(t, 0, NULL);
    // T takes the core from L, so L is queued and not running while T runs
    TEST_WAIT_FOR(status_of(t) == RUNNING && status_of(l) != RUNNING);
    run_clocks(l, &l_before);
    run_clocks(m, &m_before);
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(t, NULL, &timeout);
    run_clocks(t, &t_after);
    run_clocks(l, &l_after);
    run_clocks(m, &m_after);
    const SceUInt32 m_status = status_of(m);
    sceKernelDeleteThread(t);
    l_stop = 1;
    join_bounded(l);
    join_bounded(m);
    PRINTF((SceInt64)(l_after - l_before) < QUIET_US,
        "%s: L's run clocks grew by less than 100 ms during T's run", name);
    PRINTF((SceInt64)(t_after - t_before) > GROWTH_US,
        "%s: T's run clocks grew by more than 250 ms", name);
    PRINTF(m_after == m_before, "%s: M's run clocks did not grow", name);
    PRINTF(m_status, "%s: M's status once T has ended", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);
    run_case("M at 0xA0", 0xA0);
    run_case("M at 0x70", 0x70);
    test_exit();
    return 0;
}
