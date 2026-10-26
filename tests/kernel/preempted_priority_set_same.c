// Whether a priority change made to a preempted thread puts it behind the thread of its priority
// that was ready behind it on its core. L spins on core 0 without syscalls at a priority X, and
// M, also at X, is started from core 1 behind it, so M is queued. T, at 0x70, waits on core 0 for
// a semaphore that the main thread, on core 1, signals. T takes core 0 from L, which is preempted,
// then makes the changes to L itself, reads the statuses of L and M, and ends. Since L never
// yields, M can only run before L stops if it got the core first, so the main thread waits 300 ms
// after T's end and then stops L. Each case prints the result of the changes, the statuses of L
// and M just before T ends, and which of the two runs first: 1 for L and 2 for M. The cases are no
// change, a change of L to X itself, a change to X + 1 and back to X, and a change to X - 1 and
// back to X. X is 0xB0, in the queue the cores share, and 0x78, in the queue of core 0. The changes
// are made by T, which runs on core 0, so they come from the thread's own core.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define T_PRIORITY 0x70
// The spinner stops on its flag long before this cap, which only ends a wrong run
#define SPINS 2000000000
#define WINDOW_US 300000
#define READY 0x2
#define STANDBY 0x4
#define DORMANT 0x10

static SceUID sema, l, m, t;
static volatile int l_up, l_stop, t_done, ran, change_result;
static volatile int change_first, change_second;
static volatile SceUInt32 l_status, m_status;
static volatile int order[2];

static void record(int who) {
    order[__atomic_fetch_add(&ran, 1, __ATOMIC_SEQ_CST)] = who;
}

// L, which spins on core 0 until the test stops it, then records that it ran
static int l_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    l_up = 1;
    for (volatile int n = 0; n < SPINS && !l_stop; n++)
        ;
    record(1);
    return 0;
}

// M, which records that it ran
static int m_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    record(2);
    return 0;
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

// Whether thread has been made ready, STANDBY or READY, or has run and ended already
static int made_ready(SceUID thread) {
    const SceUInt32 status = status_of(thread);
    return status == READY || status == STANDBY || status == DORMANT;
}

// Waits for thread to end, for at most TEST_WAIT_US, then deletes it
static void join_bounded(SceUID thread) {
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);
}

// T, which waits for its semaphore, makes the changes to L, then reads the statuses of L and M
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    change_result = 0;
    if (change_first)
        change_result = sceKernelChangeThreadPriority(l, change_first);
    if (change_second)
        change_result |= sceKernelChangeThreadPriority(l, change_second);
    l_status = status_of(l);
    m_status = status_of(m);
    t_done = 1;
    return 0;
}

// Runs one case, in which T makes the changes first and then second to L, unless they are 0
static void run_case(const char *name, int priority, int first, int second) {
    ran = 0;
    order[0] = order[1] = 0;
    l_up = l_stop = t_done = 0;
    change_first = first;
    change_second = second;
    change_result = 0;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    t = sceKernelCreateThread("t", t_entry, T_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0,
                              NULL);
    sceKernelStartThread(t, 0, NULL);
    test_wait_until_waiting(t);
    l = sceKernelCreateThread("l", l_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(l, 0, NULL);
    TEST_WAIT_FOR(l_up);
    m = sceKernelCreateThread("m", m_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(m, 0, NULL);
    TEST_WAIT_FOR(made_ready(m));
    sceKernelSignalSema(sema, 1);
    TEST_WAIT_FOR(t_done);
    // T has ended, so from here on only L and M can run
    join_bounded(t);
    sceKernelDelayThread(WINDOW_US);
    l_stop = 1;
    TEST_WAIT_FOR(ran == 2);
    join_bounded(l);
    join_bounded(m);
    sceKernelDeleteSema(sema);
    if (first)
        PRINTF(change_result, "%s, L and M at 0x%X: the changes", name, priority);
    PRINTF(l_status, "%s, L and M at 0x%X: L's status before T ends", name, priority);
    PRINTF(m_status, "%s, L and M at 0x%X: M's status before T ends", name, priority);
    PRINTF(order[0], "%s, L and M at 0x%X: the first to run, 1 for L and 2 for M", name, priority);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    static const int priorities[2] = { 0xB0, 0x78 };
    for (int p = 0; p < 2; p++) {
        const int x = priorities[p];
        run_case("no change", x, 0, 0);
        run_case("set to its own value", x, x, 0);
        run_case("lowered one step and back", x, x + 1, x);
        run_case("raised one step and back", x, x - 1, x);
    }
    test_exit();
    return 0;
}
