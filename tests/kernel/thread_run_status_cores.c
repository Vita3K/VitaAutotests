// What the per core entries of sceKernelGetThreadRunStatus hold while known threads run on known
// cores. Spinner threads count in a loop until the main thread sets their stop flag. The main
// thread is on core 2 at priority 0x60 and reads the status after each step, into a zeroed struct
// whose size is set. Steps:
// - spinner S0 at 0x70 on core 0 and spinner S1 at 0xB0 on core 1 are running
// - the main thread raises S0 to 0x68, and the raise prints its result
// - spinner P at 0x50 starts on core 0, which takes the core from S0, so S0 is ready
// - S1 is stopped, so core 1 has no test thread left
// Each step prints the call's result. For each of cores 0 to 2 that should hold a test thread, it
// prints whether the entry holds that thread, whether its process is this one, and whether its
// priority is the thread's own priority from its info. Core 1 after S1 ended, and core 3 at every
// step, only print whether the entry holds none of the test's threads.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define SPINS 400000000
#define SPINNERS 3
#define NO_THREAD (-1)

static volatile int spinner_up[SPINNERS], spinner_stop[SPINNERS];
// The UID of each spinner started so far, NO_THREAD for the rest
static SceUID spinners[SPINNERS];
static SceUID main_thread;

static int spinner_entry(SceSize args, void *argp) {
    (void)args;
    const int slot = *(const int *)argp;
    spinner_up[slot] = 1;
    for (volatile int n = 0; n < SPINS && !spinner_stop[slot]; n++)
        ;
    return 0;
}

// Starts a spinner of priority on mask in slot, and returns once it runs
static void spinner_start(int slot, int priority, SceUInt32 mask) {
    spinner_up[slot] = spinner_stop[slot] = 0;
    spinners[slot] = sceKernelCreateThread("spinner", spinner_entry, priority, 0x4000, 0, mask, NULL);
    sceKernelStartThread(spinners[slot], sizeof(slot), &slot);
    TEST_WAIT_FOR(spinner_up[slot]);
}

// Stops the spinner in slot, waits for it to end and deletes it
static void spinner_end(int slot) {
    spinner_stop[slot] = 1;
    sceKernelWaitThreadEnd(spinners[slot], NULL, NULL);
    sceKernelDeleteThread(spinners[slot]);
}

// Returns whether uid is the main thread or a spinner that was started
static int is_test_thread(SceUID uid) {
    if (uid == main_thread)
        return 1;
    for (int i = 0; i < SPINNERS; i++) {
        if (spinners[i] != NO_THREAD && uid == spinners[i])
            return 1;
    }
    return 0;
}

// Returns the current priority of thread, from its info
static int priority_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.currentPriority;
}

// Reads the run status and prints the checks of one step. expected holds the thread each core
// should hold, or NO_THREAD where it should hold none of the test's threads.
static void check_step(const char *step, const SceUID expected[4]) {
    SceKernelThreadRunStatus status;
    sceClibMemset(&status, 0, sizeof(status));
    status.size = sizeof(status);
    const int result = get_thread_run_status(&status);
    PRINTF(result, "%s: the call", step);
    for (int core = 0; core < 4; core++) {
        const SceUID held = status.cpuInfo[core].threadId;
        if (expected[core] == NO_THREAD) {
            PRINTF(!is_test_thread(held), "%s: core %d: the entry holds none of the test's threads", step, core);
            continue;
        }
        PRINTF(held == expected[core], "%s: core %d: the entry is the expected thread", step, core);
        PRINTF(status.cpuInfo[core].processId == sceKernelGetProcessId(), "%s: core %d: its process is this one", step, core);
        PRINTF(status.cpuInfo[core].priority == priority_of(expected[core]), "%s: core %d: its priority is the thread's own", step, core);
    }
}

int main(void) {
    main_thread = sceKernelGetThreadId();
    for (int i = 0; i < SPINNERS; i++)
        spinners[i] = NO_THREAD;
    sceKernelChangeThreadPriority(main_thread, 0x60);
    sceKernelChangeThreadCpuAffinityMask(main_thread, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);

    spinner_start(0, 0x70, SCE_KERNEL_CPU_MASK_USER_0);
    spinner_start(1, 0xB0, SCE_KERNEL_CPU_MASK_USER_1);
    const SceUID running[4] = { spinners[0], spinners[1], main_thread, NO_THREAD };
    check_step("S0 and S1 running", running);

    PRINT("raise S0 to 0x68: the change", sceKernelChangeThreadPriority(spinners[0], 0x68));
    check_step("S0 raised to 0x68", running);

    spinner_start(2, 0x50, SCE_KERNEL_CPU_MASK_USER_0);
    const SceUID preempted[4] = { spinners[2], spinners[1], main_thread, NO_THREAD };
    check_step("P preempts core 0", preempted);

    spinner_end(1);
    const SceUID idle[4] = { spinners[2], NO_THREAD, main_thread, NO_THREAD };
    check_step("S1 ended", idle);

    spinner_end(2);
    spinner_end(0);
    test_exit();
    return 0;
}
