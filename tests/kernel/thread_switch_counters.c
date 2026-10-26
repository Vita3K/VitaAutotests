// Counters of thread info for how a thread switched out and moved between cores: interrupt
// preemptions, thread preemptions, releases (a wake from a wait) and CPU changes. The test thread T
// runs on core 0 and waits for a command from the main thread, which runs on core 1. T runs each
// command in turn: five sleeps on a semaphore that main signals, a spin while a 0x60 thread on
// core 0 wakes five times, a spin of 300 ms with nothing else ready, and a spin that is suspended
// and resumed for ForVM. A spin or a sleep reads the counters itself at its start and at its end,
// while the other threads of the test are blocked. The main thread reads them while T waits. Then
// T is moved to core 2 and back to core 0, ended, and started again, and the counters after the
// restart are compared with the ones before it.
// The cases print comparisons only, since the interrupt count depends on the timer ticks.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define T_PRIORITY 0xA0
#define HOLDER_PRIORITY 0x60
#define SLEEPS 5
#define CYCLES 5
#define SPIN_ALONE_US 300000
// How long a spin runs at most, when the main thread doesn't stop it first
#define SPIN_CAP_US 10000000
// The status bit of a thread suspended for ForVM
#define STATUS_SUSPENDED 0x100

// The commands the main thread gives T, one per wake of its command semaphore
enum { CMD_SLEEP = 1, CMD_SPIN, CMD_END };

// The counters of thread info that the test compares
typedef struct Counters {
    SceUInt32 intr;
    SceUInt32 preempt;
    SceUInt32 release;
    SceInt32 change_cpu;
} Counters;

static SceUID cmd_sema, nap_sema, cycle_sema;
static volatile int command, slept, spinning, stop, woke, holder_quit;
static Counters run_start, run_end;

// Reads the counters of thread into c. T reads itself with UID 0.
static void read_counters(SceUID thread, Counters *c) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    c->intr = info.intrPreemptCount;
    c->preempt = info.threadPreemptCount;
    c->release = info.threadReleaseCount;
    c->change_cpu = info.changeCpuCount;
}

// Returns whether thread is suspended for ForVM
static int suspended(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && (info.status & STATUS_SUSPENDED) != 0;
}

// Waits on the cycle semaphore, and counts each wake the main thread gives it
static int holder_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    while (!holder_quit) {
        SceUInt timeout = TEST_WAIT_US;
        if (sceKernelWaitSema(cycle_sema, 1, &timeout) == 0 && !holder_quit)
            woke++;
    }
    return 0;
}

// Spins until the main thread sets stop, or for SPIN_CAP_US at most
static void spin_until_stop(void) {
    const SceInt64 end = sceKernelGetSystemTimeWide() + SPIN_CAP_US;
    while (!stop && sceKernelGetSystemTimeWide() < end)
        ;
}

// Sleeps once for each wake of the nap semaphore, with the counters taken at its start and end
static void sleep_run(void) {
    read_counters(0, &run_start);
    for (int i = 0; i < SLEEPS; i++) {
        SceUInt timeout = TEST_WAIT_US;
        if (sceKernelWaitSema(nap_sema, 1, &timeout) == 0)
            slept++;
    }
    read_counters(0, &run_end);
}

// Spins until stopped, with the counters taken at its start and end
static void spin_run(void) {
    read_counters(0, &run_start);
    spinning = 1;
    spin_until_stop();
    read_counters(0, &run_end);
    spinning = 0;
}

// Waits for a command and runs it, until the main thread gives the end command
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (;;) {
        SceUInt timeout = 3 * TEST_WAIT_US;
        if (sceKernelWaitSema(cmd_sema, 1, &timeout) < 0)
            continue;
        if (command == CMD_END)
            return 0;
        if (command == CMD_SLEEP)
            sleep_run();
        else
            spin_run();
    }
}

int main(void) {
    const int orig_mask = sceKernelGetThreadCpuAffinityMask(0);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    TEST_WAIT_FOR(sceKernelGetCpuId() == 1);

    cmd_sema = sceKernelCreateSema("cmd", 0, 0, 1, NULL);
    nap_sema = sceKernelCreateSema("nap", 0, 0, 1, NULL);
    cycle_sema = sceKernelCreateSema("cycle", 0, 0, 1, NULL);
    const SceUID holder = sceKernelCreateThread("holder", holder_entry, HOLDER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID t = sceKernelCreateThread("t", t_entry, T_PRIORITY, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(holder, 0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(holder, WAIT_TYPE_SEMA, cycle_sema));
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, cmd_sema));

    // Five sleeps, each ended by a signal from the main thread
    command = CMD_SLEEP;
    sceKernelSignalSema(cmd_sema, 1);
    for (int i = 0; i < SLEEPS; i++) {
        TEST_WAIT_FOR(slept == i && test_in_wait(t, WAIT_TYPE_SEMA, nap_sema));
        sceKernelSignalSema(nap_sema, 1);
    }
    TEST_WAIT_FOR(slept == SLEEPS && test_in_wait(t, WAIT_TYPE_SEMA, cmd_sema));
    PRINT("sleeps: the thread woke five times", slept == SLEEPS);
    PRINT("sleeps: the release count grew by at least 3", (int)(run_end.release - run_start.release) >= 3);

    // The holder wakes five times while T spins on core 0, so T is preempted each time
    stop = 0;
    command = CMD_SPIN;
    sceKernelSignalSema(cmd_sema, 1);
    TEST_WAIT_FOR(spinning);
    for (int i = 0; i < CYCLES; i++) {
        TEST_WAIT_FOR(woke == i && test_in_wait(holder, WAIT_TYPE_SEMA, cycle_sema));
        sceKernelSignalSema(cycle_sema, 1);
    }
    TEST_WAIT_FOR(woke == CYCLES && test_in_wait(holder, WAIT_TYPE_SEMA, cycle_sema));
    stop = 1;
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, cmd_sema));
    PRINT("spin with a holder: the holder woke five times", woke == CYCLES);
    PRINT("spin with a holder: the preempt count grew by at least 3", (int)(run_end.preempt - run_start.preempt) >= 3);
    PRINT("spin with a holder: the release count did not grow", run_end.release == run_start.release);

    // A spin of 300 ms with nothing else ready on core 0
    stop = 0;
    command = CMD_SPIN;
    sceKernelSignalSema(cmd_sema, 1);
    TEST_WAIT_FOR(spinning);
    sceKernelDelayThread(SPIN_ALONE_US);
    stop = 1;
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, cmd_sema));
    PRINT("spin alone for 300 ms: the interrupt count grew", (int)(run_end.intr - run_start.intr) >= 1);

    // A ForVM suspend of the spinning thread, read while it is suspended, then a resume
    stop = 0;
    command = CMD_SPIN;
    sceKernelSignalSema(cmd_sema, 1);
    TEST_WAIT_FOR(spinning);
    PRINT("suspend the spinning thread for ForVM: result", sceKernelSuspendThreadForVM(t));
    TEST_WAIT_FOR(suspended(t));
    Counters suspended_counters;
    read_counters(t, &suspended_counters);
    PRINT("suspended while spinning: the interrupt count grew", (int)(suspended_counters.intr - run_start.intr) >= 1);
    PRINT("resume it: result", sceKernelResumeThreadForVM(t));
    stop = 1;
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, cmd_sema));

    // The thread moves to core 2 and back to core 0 while it waits
    PRINT("move it to core 2: result", sceKernelChangeThreadCpuAffinityMask(t, SCE_KERNEL_CPU_MASK_USER_2));
    PRINT("move it back to core 0: result", sceKernelChangeThreadCpuAffinityMask(t, SCE_KERNEL_CPU_MASK_USER_0));
    SceKernelThreadInfo moved = { .size = sizeof(moved) };
    sceKernelGetThreadInfo(t, &moved);
    PRINT("after the moves: the CPU change count is 0", moved.changeCpuCount == 0);
    PRINT("after the moves: the affinity is core 0", moved.currentCpuAffinityMask == SCE_KERNEL_CPU_MASK_USER_0);

    // End the thread and start it again, then compare its counters with the ones before the end
    Counters before, after;
    read_counters(t, &before);
    command = CMD_END;
    sceKernelSignalSema(cmd_sema, 1);
    SceUInt timeout = TEST_WAIT_US;
    PRINT("end it: result", sceKernelWaitThreadEnd(t, NULL, &timeout));
    PRINT("restart it: result", sceKernelStartThread(t, 0, NULL));
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, cmd_sema));
    command = CMD_END;
    sceKernelSignalSema(cmd_sema, 1);
    timeout = TEST_WAIT_US;
    PRINT("restarted run: end result", sceKernelWaitThreadEnd(t, NULL, &timeout));
    read_counters(t, &after);
    PRINT("restart: the interrupt count was not reset", after.intr >= before.intr);
    PRINT("restart: the preempt count was not reset", after.preempt >= before.preempt);
    PRINT("restart: the release count was not reset", after.release >= before.release);

    holder_quit = 1;
    sceKernelSignalSema(cycle_sema, 1);
    timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(holder, NULL, &timeout);
    sceKernelDeleteThread(holder);
    sceKernelDeleteThread(t);
    sceKernelDeleteSema(cmd_sema);
    sceKernelDeleteSema(nap_sema);
    sceKernelDeleteSema(cycle_sema);
    sceKernelChangeThreadCpuAffinityMask(0, orig_mask);
    test_exit();
    return 0;
}
