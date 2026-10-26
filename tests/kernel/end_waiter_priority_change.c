// The order in which two threads that wait for the end of a thread run once it ends, and what
// setting one of them to the priority it already has does to that order. Everything runs on core 0.
// The main thread is at 0x60, above the others, so they run only when it blocks. T, at 0x80, waits
// on a gate. W2, then W1, both at 0x80, wait for T's end. The main thread then opens the gate and
// waits for T's end, and the waiters run when T has ended. Two cases:
// - the priorities left alone, which gives the arrival order, W2 then W1
// - W2 set to 0x80 while both wait, which the main thread does before it opens the gate
// Each case prints the change's result, the run order of W1 and W2 as two hex digits (1 for W1, 2
// for W2, the first to run in the high digit), and the result of each wait.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0x80

static SceUID gate, target;
static volatile int run_count, run_order[2], wait_result[2];

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// Waits for the end of target, then records where it ran in the order of the waiters
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    const int digit = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    wait_result[digit - 1] = sceKernelWaitThreadEnd(target, NULL, &timeout);
    const int n = __atomic_fetch_add(&run_count, 1, __ATOMIC_SEQ_CST);
    if (n < 2)
        run_order[n] = digit;
    return 0;
}

// Starts a waiter for the end of target, numbered digit, on core 0 at the waiter priority
static SceUID start_waiter(int digit) {
    const SceUID thread = sceKernelCreateThread("waiter", waiter_entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, sizeof(digit), &digit);
    return thread;
}

// Runs one case, setting W2 to its own priority while both wait if change is 1
static void run_case(int change, const char *name) {
    run_count = 0;
    run_order[0] = run_order[1] = 0xF;
    wait_result[0] = wait_result[1] = 0xDEAD;
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    target = sceKernelCreateThread("target", target_entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(target, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(target, WAIT_TYPE_SEMA, gate));

    const SceUID w2 = start_waiter(2);
    TEST_WAIT_FOR(test_in_wait(w2, WAIT_TYPE_THREAD_END, target));
    const SceUID w1 = start_waiter(1);
    TEST_WAIT_FOR(test_in_wait(w1, WAIT_TYPE_THREAD_END, target));

    int changed = 0;
    if (change)
        changed = sceKernelChangeThreadPriority(w2, WAITER_PRIORITY);
    sceKernelSignalSema(gate, 1);
    test_join_thread(target);
    test_join_thread(w1);
    test_join_thread(w2);
    sceKernelDeleteSema(gate);

    if (change)
        PRINTF(changed, "%s: the change of W2 to its own priority", name);
    PRINTF((run_order[0] << 4) | run_order[1], "%s: run order", name);
    PRINTF(wait_result[0], "%s: W1's wait result", name);
    PRINTF(wait_result[1], "%s: W2's wait result", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case(0, "priorities left alone");
    run_case(1, "W2 set to its own priority while both wait");

    test_exit();
    return 0;
}
