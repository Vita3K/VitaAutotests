// Whether a thread that a running thread wakes, at the same priority on the same core, runs before
// the waker's next line. The waiter W waits on a semaphore, and the signaler S signals it, both
// pinned to core 0 at the same priority. The main thread is above them, also on core 0, and starts
// W, waits until it is waiting, starts S, then joins S. S records 0, signals the semaphore, records
// 2, then waits on a second semaphore. W records 1 when its wait returns, and signals the second
// semaphore so S can end. The steps should be 0 2 1: the wake doesn't switch to W until S waits.
// Cases: W and S both at 0x70, and both at 0xA0.
//
// Each case prints the result of S's signal, the result of W's wait, and the steps in the order
// they were recorded.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define MAX_STEPS 4

static SceUID first_sema, second_sema, waiter, signaler;
static int step_count, steps[MAX_STEPS];
static volatile int signal_result, wait_result;

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < MAX_STEPS)
        steps[i] = step;
}

// Waits for the first semaphore, records 1, then signals the second one
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    wait_result = sceKernelWaitSema(first_sema, 1, &timeout);
    record(1);
    sceKernelSignalSema(second_sema, 1);
    return 0;
}

// Records 0, signals the first semaphore, records 2, then waits for the second one
static int signaler_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    record(0);
    signal_result = sceKernelSignalSema(first_sema, 1);
    record(2);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(second_sema, 1, &timeout);
    return 0;
}

static void print_steps(const char *name) {
    const int count = step_count < MAX_STEPS ? step_count : MAX_STEPS;
    PRINTF(step_count, "%s: steps recorded", name);
    for (int i = 0; i < count; i++)
        PRINTF(steps[i], "%s: step %d", name, i);
}

static void run_case(const char *name, int priority) {
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = -1;
    signal_result = wait_result = 0xDEAD;
    first_sema = sceKernelCreateSema("first", 0, 0, 1, NULL);
    second_sema = sceKernelCreateSema("second", 0, 0, 1, NULL);
    waiter = sceKernelCreateThread("waiter", waiter_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    signaler = sceKernelCreateThread("signaler", signaler_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);

    sceKernelStartThread(waiter, 0, NULL);
    test_wait_until_waiting(waiter);
    sceKernelStartThread(signaler, 0, NULL);

    // Joined first, so every record the threads made is complete
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(signaler, NULL, &timeout);
    timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(waiter, NULL, &timeout);
    sceKernelDeleteThread(signaler);
    sceKernelDeleteThread(waiter);
    sceKernelDeleteSema(first_sema);
    sceKernelDeleteSema(second_sema);

    PRINTF(signal_result, "%s: the signal", name);
    PRINTF(wait_result, "%s: the waiter's result", name);
    print_steps(name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("both at 0x70", 0x70);
    run_case("both at 0xA0", 0xA0);

    test_exit();
    return 0;
}
