// The run clocks of a thread in sceKernelGetThreadInfo: how much they grow while the thread spins,
// whether a delay while it waits adds to them, whether a restart keeps the total going, and whether
// they stay put once the thread ended. The thread waits on a gate, spins for half a second, then
// waits on a second semaphore, so every read is taken while it waits or after it ended.
// The cases: the growth over the spin, the growth over a one second delay while it waits, the
// second run's total against the first's, and the two reads taken 100 ms apart after the end.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// How long each run spins, and the margin the comparisons use around the expected growth
#define SPIN_US 500000
#define GROWTH_US 250000
#define DELAY_US 1000000

static SceUID go, rest;
static volatile int spun;

// Busy waits for us microseconds, without a call that waits
static void spin(SceUInt64 us) {
    const SceInt64 start = sceKernelGetSystemTimeWide();
    while (sceKernelGetSystemTimeWide() - start < (SceInt64)us) {
    }
}

// Waits for the gate, spins, then waits for the rest semaphore, in every run
static int clocks_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(go, 1, &timeout);
    spin(SPIN_US);
    spun++;
    timeout = TEST_WAIT_US;
    sceKernelWaitSema(rest, 1, &timeout);
    return 0;
}

// Reads the run clocks of thread into clocks, and returns the result of the info call
static int run_clocks(SceUID thread, SceUInt64 *clocks) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    const int result = sceKernelGetThreadInfo(thread, &info);
    *clocks = info.runClocks;
    return result;
}

int main(void) {
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    rest = sceKernelCreateSema("rest", 0, 0, 1, NULL);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    const SceUID thread = sceKernelCreateThread("clocks", clocks_thread, 0x60, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    SceUInt64 at_gate = 0, after_spin = 0, after_delay = 0, first_end = 0, second_spin = 0;
    SceUInt64 end_first = 0, end_later = 0;

    // The first run: the reads at the gate and after the spin
    sceKernelStartThread(thread, 0, NULL);
    test_wait_until_waiting(thread);
    PRINT("first run: info at the gate", run_clocks(thread, &at_gate));
    sceKernelSignalSema(go, 1);
    TEST_WAIT_FOR(spun == 1);
    test_wait_until_waiting(thread);
    PRINT("first run: info after the spin", run_clocks(thread, &after_spin));
    PRINT("the spin grew the run clocks by at least 250 ms", (SceInt64)(after_spin - at_gate) >= GROWTH_US);

    // A delay while the thread waits
    sceKernelDelayThread(DELAY_US);
    PRINT("delay: info", run_clocks(thread, &after_delay));
    PRINT("the one second delay grew the run clocks by less than 250 ms", (SceInt64)(after_delay - after_spin) < GROWTH_US);

    // The first run ends, then the thread starts again and spins once more
    sceKernelSignalSema(rest, 1);
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    PRINT("first run ended: info", run_clocks(thread, &first_end));
    sceKernelStartThread(thread, 0, NULL);
    test_wait_until_waiting(thread);
    sceKernelSignalSema(go, 1);
    TEST_WAIT_FOR(spun == 2);
    test_wait_until_waiting(thread);
    PRINT("second run: info after the spin", run_clocks(thread, &second_spin));
    PRINT("the second run's total is above the first run's by at least 250 ms", (SceInt64)(second_spin - first_end) >= GROWTH_US);

    // Ended, then read twice 100 ms apart
    sceKernelSignalSema(rest, 1);
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    PRINT("ended: info", run_clocks(thread, &end_first));
    sceKernelDelayThread(100000);
    PRINT("ended: info 100 ms later", run_clocks(thread, &end_later));
    PRINT("the two reads after the end are equal", end_first == end_later);
    sceKernelDeleteThread(thread);

    test_exit();
    return 0;
}
