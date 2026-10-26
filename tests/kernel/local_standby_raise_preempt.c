// Whether a thread that raises another thread of its own core above itself, or to its own
// priority, gives the core to that thread before the call returns. S runs on core 0 at 0x70 or
// 0xA0 and spins until the main thread on core 1 lets it go. T has a mask of core 0 alone and is 8
// above S. T is made ready in one of two ways:
// - started from core 1 while S runs, so it waits on core 0 (STANDBY at 0x78, READY at 0xA8)
// - woken by S from a semaphore that T waits on, so it is READY behind S
// Then S reads T's status, records 0, raises T to a new priority, and records 2 once the call
// returns. T records 1 when it runs. Each case prints S's read of T's status, the result of the
// change, and the steps in the order they were recorded. The cases are: T raised to 0x68 from
// STANDBY, to 0x70 from STANDBY (a tie with S), and to 0x68 from READY when woken by S. Then the
// same three at 0xA0, with T at 0xA8 raised to 0x98, to 0xA0, and to 0x98 when woken.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define SPINS 400000000
#define MAX_STEPS 4
#define STATUS_READY 0x2
#define STATUS_STANDBY 0x4

enum How { STARTED_FROM_CORE_1, WOKEN_BY_S };

static SceUID sema, target;
static volatile int s_up, go, s_new_priority;
static volatile int s_status, s_result;
static int step_count, steps[MAX_STEPS];

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < MAX_STEPS)
        steps[i] = step;
}

// T waits for the semaphore first when started with an argument, then records 1
static int t_entry(SceSize args, void *argp) {
    (void)argp;
    if (args) {
        SceUInt32 timeout = 3 * TEST_WAIT_US;
        sceKernelWaitSema(sema, 1, &timeout);
    }
    record(1);
    return 0;
}

// S spins on core 0 until it is let go. It signals T first when its argument is nonzero.
static int s_entry(SceSize args, void *argp) {
    s_up = 1;
    for (volatile int n = 0; n < SPINS && !go; n++)
        ;
    if (args >= sizeof(int) && *(const int *)argp)
        sceKernelSignalSema(sema, 1);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(target, &info);
    s_status = info.status;
    record(0);
    s_result = sceKernelChangeThreadPriority(target, s_new_priority);
    record(2);
    return 0;
}

// Waits for thread to end, for at most 3 seconds, then deletes it
static void join_or_timeout(SceUID thread) {
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);
}

static int status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

static void run_case(const char *name, int s_priority, enum How how, int new_priority) {
    go = s_up = 0;
    s_status = s_result = 0;
    s_new_priority = new_priority;
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = -1;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID s = sceKernelCreateThread("s", s_entry, s_priority, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID t = sceKernelCreateThread("t", t_entry, s_priority + 8, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    target = t;
    const int woken = how == WOKEN_BY_S;
    if (woken) {
        // Core 0 is idle, so T runs at once and waits on the semaphore
        int t_arg = 1;
        sceKernelStartThread(t, sizeof(t_arg), &t_arg);
        TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, sema));
    }
    int s_arg = woken;
    sceKernelStartThread(s, sizeof(s_arg), &s_arg);
    TEST_WAIT_FOR(s_up);
    if (!woken) {
        // Started while S holds core 0, so T waits there as STANDBY or READY
        sceKernelStartThread(t, 0, NULL);
        TEST_WAIT_FOR(status_of(t) == STATUS_STANDBY || status_of(t) == STATUS_READY);
    }
    go = 1;
    join_or_timeout(s);
    join_or_timeout(t);
    sceKernelDeleteSema(sema);

    const int count = step_count < MAX_STEPS ? step_count : MAX_STEPS;
    PRINTF(s_status, "%s: S's read of T's status before the change", name);
    PRINTF(s_result, "%s: the change's result", name);
    for (int i = 0; i < count; i++)
        PRINTF(steps[i], "%s: step %d", name, i);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("S at 0x70, T started from core 1 at 0x78, raised to 0x68", 0x70, STARTED_FROM_CORE_1,
             0x68);
    run_case("S at 0x70, T started from core 1 at 0x78, raised to 0x70", 0x70, STARTED_FROM_CORE_1,
             0x70);
    run_case("S at 0x70, T woken by S at 0x78, raised to 0x68", 0x70, WOKEN_BY_S, 0x68);
    run_case("S at 0xA0, T started from core 1 at 0xA8, raised to 0x98", 0xA0, STARTED_FROM_CORE_1,
             0x98);
    run_case("S at 0xA0, T started from core 1 at 0xA8, raised to 0xA0", 0xA0, STARTED_FROM_CORE_1,
             0xA0);
    run_case("S at 0xA0, T woken by S at 0xA8, raised to 0x98", 0xA0, WOKEN_BY_S, 0x98);

    test_exit();
    return 0;
}
