// The per core counters of sceKernelGetSystemInfo over windows of one second: the idle clock, the
// comes-out-of-idle count and the thread switch count. For each user core in turn, the main thread
// moves to another user core, so the measured core only runs the case's threads. Each case takes
// its snapshots of the counters before and after its window:
// - one spinner alone on the core for the window: the thread switch count grows by less than 10,
//   and the idle clock grows by less than half the window
// - nothing on the core for the window, then a thread that starts and ends on the core, so the
//   second snapshot is taken after the core's idle time is up to date: the idle clock grows by at
//   least half the window
// - a thread that delays 50 ms 20 times on the core: the comes-out-of-idle count grows by at least
//   10, and the thread switch count grows by at least 10, half the wakes, so a wake that doesn't
//   pass through idle doesn't change the line
// - two spinners of equal priority on the core: the thread switch count grows by less than 10, and
//   the idle clock grows by less than half the window
// The window is measured with the process time, and the idle clock is compared with it, so the two
// are assumed to share a unit. Each line is a boolean.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define SPINS 400000000
#define WINDOW_US 1000000
#define WAKES 20
// Half the wakes, the threshold for the counters that grow with them
#define WAKES_THRESHOLD (WAKES / 2)
#define SPINNER_PRIORITY 0x70

typedef struct Snapshot {
    SceUInt64 time;
    SceKernelSystemInfo info;
} Snapshot;

static volatile int spinner_up[2], spinner_stop[2];
static SceUID spinners[2];

static int spinner_entry(SceSize args, void *argp) {
    (void)args;
    const int slot = *(const int *)argp;
    spinner_up[slot] = 1;
    for (volatile int n = 0; n < SPINS && !spinner_stop[slot]; n++)
        ;
    return 0;
}

// Starts a spinner of SPINNER_PRIORITY on mask in slot, without waiting for it to run
static void spinner_start(int slot, SceUInt32 mask) {
    spinner_up[slot] = spinner_stop[slot] = 0;
    spinners[slot] = sceKernelCreateThread("spinner", spinner_entry, SPINNER_PRIORITY, 0x4000, 0, mask, NULL);
    sceKernelStartThread(spinners[slot], sizeof(slot), &slot);
}

// Waits for the spinner in slot to end and deletes it
static void spinner_join(int slot) {
    sceKernelWaitThreadEnd(spinners[slot], NULL, NULL);
    sceKernelDeleteThread(spinners[slot]);
}

static int delays_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (int i = 0; i < WAKES; i++)
        sceKernelDelayThread(50000);
    return 0;
}

static int quick_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Starts entry on mask at the default priority, and waits for it to end
static void run_on(SceKernelThreadEntry entry, SceUInt32 mask) {
    const SceUID thread = sceKernelCreateThread("test", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, mask, NULL);
    sceKernelStartThread(thread, 0, NULL);
    test_join_thread(thread);
}

// Reads the process time, then the system info
static void take(Snapshot *snap) {
    snap->time = sceKernelGetProcessTimeWide();
    sceClibMemset(&snap->info, 0, sizeof(snap->info));
    snap->info.size = sizeof(snap->info);
    sceKernelGetSystemInfo(&snap->info);
}

// Returns whether the idle clock of core grew by at least half the window between the snapshots
static int idle_at_least_half(const Snapshot *before, const Snapshot *after, int core) {
    const SceUInt64 grew = after->info.cpuInfo[core].idleClock - before->info.cpuInfo[core].idleClock;
    return 2 * grew >= after->time - before->time;
}

// Returns how many thread switches core made between the snapshots
static SceUInt32 switches(const Snapshot *before, const Snapshot *after, int core) {
    return after->info.cpuInfo[core].threadSwitchCount - before->info.cpuInfo[core].threadSwitchCount;
}

// Returns how many times core came out of idle between the snapshots
static SceUInt32 wakes_from_idle(const Snapshot *before, const Snapshot *after, int core) {
    return after->info.cpuInfo[core].comesOutOfIdleCount - before->info.cpuInfo[core].comesOutOfIdleCount;
}

// Prints the checks of the four cases on one user core
static void measure_core(int core) {
    static const SceUInt32 masks[3] = { SCE_KERNEL_CPU_MASK_USER_0, SCE_KERNEL_CPU_MASK_USER_1, SCE_KERNEL_CPU_MASK_USER_2 };
    const SceUInt32 mask = masks[core];
    Snapshot before, after;

    // The main thread moves to a core other than the measured one, core 0 for core 2
    sceKernelChangeThreadCpuAffinityMask(0, masks[core == 2 ? 0 : 2]);
    // The main thread only moves when it is next scheduled
    sceKernelDelayThread(1000);

    spinner_start(0, mask);
    TEST_WAIT_FOR(spinner_up[0]);
    take(&before);
    sceKernelDelayThread(WINDOW_US);
    take(&after);
    spinner_stop[0] = 1;
    spinner_join(0);
    PRINTF(switches(&before, &after, core) < 10, "core %d: a spinner alone, its thread switch count grew by less than 10", core);
    PRINTF(!idle_at_least_half(&before, &after, core), "core %d: a spinner alone, its idle clock stayed under half the window", core);

    take(&before);
    sceKernelDelayThread(WINDOW_US);
    run_on(quick_entry, mask);
    take(&after);
    PRINTF(idle_at_least_half(&before, &after, core), "core %d: idle, its idle clock grew by at least half the window", core);

    take(&before);
    run_on(delays_entry, mask);
    take(&after);
    PRINTF(wakes_from_idle(&before, &after, core) >= WAKES_THRESHOLD, "core %d: 20 delay wakes, its comes-out-of-idle count grew by at least 10", core);
    PRINTF(switches(&before, &after, core) >= WAKES_THRESHOLD, "core %d: 20 delay wakes, its thread switch count grew by at least 10", core);

    // The second spinner has the same priority, so it waits behind the first one on the core
    spinner_start(0, mask);
    TEST_WAIT_FOR(spinner_up[0]);
    spinner_start(1, mask);
    take(&before);
    sceKernelDelayThread(WINDOW_US);
    take(&after);
    spinner_stop[0] = 1;
    spinner_stop[1] = 1;
    spinner_join(0);
    spinner_join(1);
    PRINTF(switches(&before, &after, core) < 10, "core %d: two spinners, its thread switch count grew by less than 10", core);
    PRINTF(!idle_at_least_half(&before, &after, core), "core %d: two spinners, its idle clock stayed under half the window", core);
}

int main(void) {
    for (int core = 0; core < 3; core++)
        measure_core(core);
    test_exit();
    return 0;
}
