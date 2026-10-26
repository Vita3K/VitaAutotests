// Where a thread that was RUNNING when it was ForVM-suspended goes in the ready queue of its core
// on resume, and whether it is READY or STANDBY there. T, a VM thread on core 0 that spins
// without syscalls, is suspended by the main thread, on core 1, while it runs. Then a gate G at
// 0x40 takes core 0, and U, of the same priority, is started from core 1 behind it, so it is
// queued. T is resumed, either by the main thread on core 1 or by G itself on core 0. Then V, of
// the same priority, is started from core 1 behind both. Once the statuses of T and U are read, T
// is told to stop spinning and the gate lifts, and the three run in the order the scheduler picks
// them. Each case prints the suspend and resume results, the statuses of T and U just before the
// gate lifts, and the order the threads ran in: 1 for T, 2 for U and 3 for V. The cases run at
// 0x70, in the queue of core 0, and at 0xA0, in the queue the cores share, each with either
// resumer. A control per priority suspends and resumes T with nothing else ready, and prints T's
// status while it is suspended and once it runs again.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_PRIORITY SCE_KERNEL_HIGHEST_PRIORITY_USER
#define RUNNING 0x1
#define READY 0x2
#define STANDBY 0x4
#define DORMANT 0x10
// The spinners stop on their flags long before this cap, which only ends a wrong run
#define SPINS 2000000000

enum Resumer { BY_MAIN, BY_GATE };

static SceUID t, gate;
static volatile int t_up, t_stop, gate_up, gate_open, resume_req, resume_done, resume_result;
static volatile int ran;
static volatile int order[3];

static void record(int who) {
    order[__atomic_fetch_add(&ran, 1, __ATOMIC_SEQ_CST)] = who;
}

// T, which spins on core 0 until the test stops it, then records that it ran
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    t_up = 1;
    for (volatile int n = 0; n < SPINS && !t_stop; n++)
        ;
    record(1);
    return 0;
}

// Spins on core 0 until the gate lifts, and resumes T when the main thread asks it to
static int gate_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    gate_up = 1;
    for (volatile int n = 0; n < SPINS && !gate_open; n++) {
        if (resume_req && !resume_done) {
            resume_result = sceKernelResumeThreadForVM(t);
            resume_done = 1;
        }
    }
    return 0;
}

// U, which records that it ran
static int u_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    record(2);
    return 0;
}

// V, which records that it ran
static int v_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    record(3);
    return 0;
}

// Creates a VM thread on core 0 at priority, and starts it
static SceUID start(SceKernelThreadEntry entry, int priority) {
    const SceUID thread = sceKernelCreateThread("t", entry, priority, 0x4000, THREAD_ATTR_VM,
                                                SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
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

// A thread that is suspended while it runs, with nothing else ready, and resumed
static void run_control(int priority) {
    t_up = t_stop = 0;
    t = start(t_entry, priority);
    TEST_WAIT_FOR(t_up);
    const int suspended = sceKernelSuspendThreadForVM(t);
    const SceUInt32 suspended_status = status_of(t);
    const int resumed = sceKernelResumeThreadForVM(t);
    TEST_WAIT_FOR(status_of(t) == RUNNING);
    const SceUInt32 t_status = status_of(t);
    t_stop = 1;
    join_bounded(t);
    PRINTF(suspended, "control, priority 0x%X: the suspend", priority);
    PRINTF(suspended_status, "control, priority 0x%X: T's status while suspended", priority);
    PRINTF(resumed, "control, priority 0x%X: the resume", priority);
    PRINTF(t_status, "control, priority 0x%X: T's status once it runs again", priority);
}

static void run_case(int priority, int resumer) {
    const char *name = resumer == BY_MAIN ? "main thread resumes T" : "gate resumes T";
    ran = 0;
    order[0] = order[1] = order[2] = 0;
    t_up = t_stop = gate_up = gate_open = resume_req = resume_done = 0;
    resume_result = 0;
    t = start(t_entry, priority);
    TEST_WAIT_FOR(t_up);
    const int suspended = sceKernelSuspendThreadForVM(t);
    gate = sceKernelCreateThread("gate", gate_entry, GATE_PRIORITY, 0x4000, 0,
                                 SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(gate, 0, NULL);
    TEST_WAIT_FOR(gate_up);
    const SceUID u = start(u_entry, priority);
    TEST_WAIT_FOR(made_ready(u));
    int resumed;
    if (resumer == BY_MAIN) {
        resumed = sceKernelResumeThreadForVM(t);
    } else {
        resume_req = 1;
        TEST_WAIT_FOR(resume_done);
        resumed = resume_result;
    }
    const SceUID v = start(v_entry, priority);
    TEST_WAIT_FOR(made_ready(v));
    const SceUInt32 t_status = status_of(t);
    const SceUInt32 u_status = status_of(u);
    t_stop = 1;
    gate_open = 1;
    join_bounded(gate);
    TEST_WAIT_FOR(ran == 3);
    // Joined only after every record was made, so the order is complete
    join_bounded(t);
    join_bounded(u);
    join_bounded(v);
    PRINTF(suspended, "%s, priority 0x%X: the suspend", name, priority);
    PRINTF(resumed, "%s, priority 0x%X: the resume", name, priority);
    PRINTF(t_status, "%s, priority 0x%X: T's status before the gate lifts", name, priority);
    PRINTF(u_status, "%s, priority 0x%X: U's status before the gate lifts", name, priority);
    PRINTF(order[0], "%s, priority 0x%X: the first to run, 1 for T, 2 for U and 3 for V", name,
           priority);
    PRINTF(order[1], "%s, priority 0x%X: the second to run", name, priority);
    PRINTF(order[2], "%s, priority 0x%X: the third to run", name, priority);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    static const int priorities[2] = { 0xA0, 0x70 };
    for (int p = 0; p < 2; p++) {
        run_control(priorities[p]);
        run_case(priorities[p], BY_MAIN);
        run_case(priorities[p], BY_GATE);
    }
    test_exit();
    return 0;
}
