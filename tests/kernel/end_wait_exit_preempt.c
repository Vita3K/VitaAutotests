// Where threads woken by another thread's end run, against a thread at their
// priority that was already ready on the same core. Core 0 is held by a gate G
// at 0x40, which spins without syscalls until the main thread lifts it. T at
// 0x70 waits on a semaphore, and is signaled behind G, so it is ready on core
// 0. W1 at 0x60 and W2 at 0x70 wait for T's end before G is lifted. In the
// first case L at 0x70 is also made ready on core 0 after T and before T ends.
// Once G is lifted, T runs and returns, which wakes W1 and W2. Each thread
// records a digit when it runs: T records 1, both waiters record 2, and L
// records 3. The run order prints as those digits, first to run first, so it
// shows where L runs among the two waiters. The waiters share a digit, since
// the order between two threads woken by the same end is not what this test
// checks. The control is the same without L. Each case prints the run order
// and the result of each waiter's end wait. The main thread runs on core 2,
// and every thread but the main thread and G runs on core 0.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define GATE_PRIORITY 0x40
#define T_PRIORITY 0x70
#define W1_PRIORITY 0x60
#define W2_PRIORITY 0x70
#define L_PRIORITY 0x70
#define READY 0x2
#define STANDBY 0x4

static SceUID sema, target, gate, w1, w2, l;
static volatile int gate_up, gate_open;
static volatile int run_count, runs[4];
static volatile int w1_result, w2_result;

// Adds digit to the run order
static void record(int digit) {
    const int n = __atomic_fetch_add(&run_count, 1, __ATOMIC_SEQ_CST);
    runs[n] = digit;
}

// Spins on core 0 until the gate is lifted
static int gate_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    gate_up = 1;
    while (!gate_open)
        ;
    return 0;
}

// Waits for the semaphore, then records and returns
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    record(1);
    return 0;
}

// Waits for T's end, then records the result
static int w1_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    w1_result = sceKernelWaitThreadEnd(target, NULL, &timeout);
    record(2);
    return 0;
}

static int w2_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    w2_result = sceKernelWaitThreadEnd(target, NULL, &timeout);
    record(2);
    return 0;
}

static int l_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    record(3);
    return 0;
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = {.size = sizeof(info)};
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

// Whether thread is ready to run on its core, STANDBY or READY
static int queued(SceUID thread) {
    const SceUInt32 status = status_of(thread);
    return status == READY || status == STANDBY;
}

// The run order so far, one hex digit per thread, the first to run in the
// highest digit
static int run_order(void) {
    int value = 0;
    for (int i = 0; i < run_count; i++)
        value = (value << 4) | runs[i];
    return value;
}

static void run_case(const char *name, int with_l) {
    run_count = 0;
    for (int i = 0; i < 4; i++)
        runs[i] = 0;
    w1_result = w2_result = 0xDEAD;
    gate_up = gate_open = 0;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    target = sceKernelCreateThread("t", t_entry, T_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0,
                                   NULL);
    sceKernelStartThread(target, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(target, WAIT_TYPE_SEMA, sema));
    w1 = sceKernelCreateThread("w1", w1_entry, W1_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0,
                               NULL);
    sceKernelStartThread(w1, 0, NULL);
    test_wait_until_waiting(w1);
    w2 = sceKernelCreateThread("w2", w2_entry, W2_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0,
                               NULL);
    sceKernelStartThread(w2, 0, NULL);
    test_wait_until_waiting(w2);
    gate = sceKernelCreateThread("gate", gate_entry, GATE_PRIORITY, 0x4000, 0,
                                 SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(gate, 0, NULL);
    TEST_WAIT_FOR(gate_up);
    sceKernelSignalSema(sema, 1);
    TEST_WAIT_FOR(queued(target));
    if (with_l) {
        l = sceKernelCreateThread("l", l_entry, L_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0,
                                  NULL);
        sceKernelStartThread(l, 0, NULL);
        TEST_WAIT_FOR(queued(l));
    }
    gate_open = 1;
    sceKernelWaitThreadEnd(gate, NULL, NULL);
    sceKernelDeleteThread(gate);
    test_join_thread(target);
    test_join_thread(w1);
    test_join_thread(w2);
    if (with_l)
        test_join_thread(l);
    sceKernelDeleteSema(sema);
    PRINTF(run_order(), "%s: run order", name);
    PRINTF(w1_result, "%s: W1's end wait result", name);
    PRINTF(w2_result, "%s: W2's end wait result", name);
}

int main(void) {
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);
    run_case("L ready before T ends", 1);
    run_case("control, without L", 0);
    test_exit();
    return 0;
}
