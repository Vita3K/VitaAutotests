// What a ForVM suspend of a running thread hands to the thread queued behind it on the same core,
// and whether that queued thread can resume it from there. T is a VM thread on core 0 that spins
// without syscalls. U is queued behind it on core 0, at T's priority or 8 below it, and is started
// either by T itself or by the main thread on core 1. The main thread then suspends T and waits up
// to 300 ms for U to say it ran. After that U records step 0, resumes T, and records step 2 once
// the resume returns. T records step 1 once it runs again. The steps are 0 1 2 when T is higher
// than U, so the resume runs T before U returns, and 0 2 1 when they are equal, so U returns first
// and T runs after it. The cases run at 0x70 and at 0xA0. Each prints the suspend's result,
// whether U ran within 300 ms of the suspend, the resume's result, and the steps in order.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAX_STEPS 4
// How long the main thread waits for U to run after the suspend
#define RAN_WAIT_US 300000

enum Start { BY_T, BY_MAIN };

static SceUID t, u, ran, gate;
static volatile int t_up, t_stop, u_made, u_resume;
static int start_by, u_priority;
static int step_count, steps[MAX_STEPS];

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < MAX_STEPS)
        steps[i] = step;
}

// U, which says it ran, waits for the gate, then resumes T with the steps around the resume
static int u_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelSignalSema(ran, 1);
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    record(0);
    u_resume = sceKernelResumeThreadForVM(t);
    record(2);
    return 0;
}

// Creates U on core 0 at u_priority, and starts it
static SceUID create_u(void) {
    const SceUID thread = sceKernelCreateThread("u", u_entry, u_priority, 0x4000, 0,
        SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

// T, which starts U first when start_by says so, then spins until the test stops it
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    t_up = 1;
    if (start_by == BY_T) {
        u = create_u();
        u_made = 1;
    }
    while (!t_stop)
        ;
    record(1);
    return 0;
}

// Waits for thread to end, for at most TEST_WAIT_US, then deletes it
static void join_bounded(SceUID thread) {
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);
}

// Runs one case, with U started by by at priority up, and T at priority
static void run_case(int by, int priority, int up) {
    char name[64];
    sceClibSnprintf(name, sizeof(name), "%s, T 0x%X, U 0x%X",
        by == BY_T ? "U started by T" : "U started by main", priority, up);
    start_by = by;
    u_priority = up;
    t_up = t_stop = u_made = u_resume = 0;
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = -1;
    ran = sceKernelCreateSema("ran", 0, 0, 1, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    t = sceKernelCreateThread("t", t_entry, priority, 0x4000, THREAD_ATTR_VM,
        SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(t_up);
    if (by == BY_MAIN) {
        u = create_u();
        u_made = 1;
    }
    TEST_WAIT_FOR(u_made);
    const int suspended = sceKernelSuspendThreadForVM(t);
    SceUInt32 timeout = RAN_WAIT_US;
    const int ran_in_time = sceKernelWaitSema(ran, 1, &timeout) == 0;
    // Stops T's loop, which only runs again once U has resumed T
    t_stop = 1;
    sceKernelSignalSema(gate, 1);
    join_bounded(u);
    join_bounded(t);
    sceKernelDeleteSema(ran);
    sceKernelDeleteSema(gate);
    PRINTF(suspended, "%s: the suspend", name);
    PRINTF(ran_in_time, "%s: U ran within 300 ms of the suspend", name);
    PRINTF(u_resume, "%s: the resume", name);
    const int count = step_count < MAX_STEPS ? step_count : MAX_STEPS;
    for (int i = 0; i < count; i++)
        PRINTF(steps[i], "%s: step %d", name, i);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    static const int priorities[2] = { 0xA0, 0x70 };
    for (int p = 0; p < 2; p++) {
        const int priority = priorities[p];
        run_case(BY_T, priority, priority);
        run_case(BY_T, priority, priority + 8);
        run_case(BY_MAIN, priority, priority);
        run_case(BY_MAIN, priority, priority + 8);
    }
    test_exit();
    return 0;
}
