// Where a thread goes when its CPU mask is narrowed to core 1, which another thread spins on. The
// change is made by the thread itself (self) or by the main thread on a spinning thread (remote).
// S runs on core 0 at 0x70 or at 0xA0, and R, 8 lower, is queued behind it on core 0 with the
// same mask. H spins on core 1 without syscalls until it is stopped, at 0x50 (higher than S), at
// S's priority (equal) or at 0xB8 (lower). Once R is queued, the change takes S off core 0 at
// once, so R can take that core, and S waits on core 1 while H holds it. Each case prints the
// change's result, then 300 ms after the change whether S's step after its own call ran (self) or
// whether S counted (remote), whether R ran, and S's status. A remote case counts S again in the
// next 300 ms. H is then stopped and 300 ms later the core S ran on is noted. A lower H is only
// stopped once S has ended, since S preempts it at once. Cases: S at 0x70 and at 0xA0, self and
// remote, each with H at 0x50, at S's priority and at 0xB8.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define SPINS 0xFFFFFFFFu
#define WINDOW_US 300000

static volatile int h_up, h_stop, s_up, s_go, s_stop, s_after, s_result, s_core, r_ran;
static volatile unsigned int s_count;

// H, which spins on core 1 until it is stopped
static int h_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    h_up = 1;
    for (volatile unsigned int n = 0; n < SPINS && !h_stop; n++)
        ;
    return 0;
}

// S of the self case, which spins on core 0 until R is queued, then narrows its own mask to core 1
static int self_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    s_up = 1;
    while (!s_go)
        ;
    s_result = sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    s_after = 1;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    s_core = info.currentCpuId;
    return 0;
}

// S of the remote case, which counts and notes its core in each loop until it is stopped
static int remote_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    s_up = 1;
    while (!s_stop) {
        SceKernelThreadInfo info = { .size = sizeof(info) };
        sceKernelGetThreadInfo(0, &info);
        s_core = info.currentCpuId;
        s_count++;
    }
    return 0;
}

// R, which records that it ran
static int r_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    r_ran = 1;
    return 0;
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

// Returns whether thread is READY or STANDBY, that is queued behind the thread that runs
static int queued(SceUID thread) {
    const SceUInt32 status = status_of(thread);
    return status == SCE_THREAD_READY || status == SCE_THREAD_STANDBY;
}

// Waits for a thread to end, for at most TEST_WAIT_US, and deletes it. A thread that doesn't end in
// time is left as it is, so a wrong result can't hang the run.
static void join_timed(SceUID thread) {
    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelWaitThreadEnd(thread, NULL, &timeout) >= 0)
        sceKernelDeleteThread(thread);
}

// Runs one case, with S at s_priority, H at h_priority, and S changing its mask itself or remotely
static void run_case(int s_priority, int h_priority, int remote) {
    const char *how = remote ? "remote" : "self";
    const int lower = h_priority > s_priority;
    h_up = h_stop = s_up = s_go = s_stop = s_after = r_ran = 0;
    s_result = -1;
    s_core = -2;
    s_count = 0;
    const SceUID h = sceKernelCreateThread("h", h_entry, h_priority, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(h, 0, NULL);
    TEST_WAIT_FOR(h_up);
    const SceUID s = sceKernelCreateThread("s", remote ? remote_entry : self_entry, s_priority,
                                           0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(s, 0, NULL);
    TEST_WAIT_FOR(s_up);
    // R is queued behind S on core 0 before S changes its mask
    const SceUID r = sceKernelCreateThread("r", r_entry, s_priority + 8, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(r, 0, NULL);
    TEST_WAIT_FOR(queued(r));
    if (remote)
        s_result = sceKernelChangeThreadCpuAffinityMask(s, SCE_KERNEL_CPU_MASK_USER_1);
    else
        s_go = 1;
    sceKernelDelayThread(WINDOW_US);
    const int after_step = s_after;
    const int r_early = r_ran;
    const SceUInt32 status = status_of(s);
    int s_moved = 0;
    if (remote) {
        const unsigned int count_before = s_count;
        sceKernelDelayThread(WINDOW_US);
        s_moved = s_count != count_before;
    }

    // A higher or equal H is stopped first, then S gets core 1 and runs. A lower H is preempted by
    // S, so S is stopped and joined first.
    if (lower) {
        if (remote)
            s_stop = 1;
        join_timed(s);
        h_stop = 1;
        join_timed(h);
    } else {
        h_stop = 1;
        join_timed(h);
        sceKernelDelayThread(WINDOW_US);
        if (remote)
            s_stop = 1;
        join_timed(s);
    }
    const int s_core_after = s_core;
    join_timed(r);

    PRINTF(s_result, "S at 0x%X, H at 0x%X, %s: the change to core 1", s_priority, h_priority, how);
    if (remote)
        PRINTF(s_moved, "S at 0x%X, H at 0x%X, %s: S counted in the second 300 ms", s_priority,
               h_priority, how);
    else
        PRINTF(after_step, "S at 0x%X, H at 0x%X, %s: S's step after the call ran in 300 ms",
               s_priority, h_priority, how);
    PRINTF(r_early, "S at 0x%X, H at 0x%X, %s: R ran within 300 ms", s_priority, h_priority, how);
    PRINTF(status, "S at 0x%X, H at 0x%X, %s: S's status 300 ms after the change", s_priority,
           h_priority, how);
    PRINTF(s_core_after, "S at 0x%X, H at 0x%X, %s: the core S ran on after H stopped", s_priority,
           h_priority, how);
}

int main(void) {
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);
    static const int s_priorities[2] = { 0x70, 0xA0 };
    for (int s = 0; s < 2; s++) {
        const int s_priority = s_priorities[s];
        // H is higher than S, equal to it, or lower
        const int h_priorities[3] = { 0x50, s_priority, 0xB8 };
        for (int remote = 0; remote < 2; remote++)
            for (int h = 0; h < 3; h++)
                run_case(s_priority, h_priorities[h], remote);
    }
    test_exit();
    return 0;
}
