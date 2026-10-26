// Whether a CPU mask change that keeps the caller on its core gives up that core to a thread of
// the same priority that is ready behind the caller. The thread R waits on a semaphore and the
// thread S runs on core 0 too. The main thread, on core 1, starts R, waits until it is waiting,
// starts S and joins both. S records 0 and signals R, so R is ready behind S. S then spins without
// syscalls until R has run, or for a fixed number of loops, changes its own mask, records 2, and
// waits for R to end. R records 1 when it runs and signals S. The steps should be 0 2 1, since the
// change doesn't yield and R only runs once S waits. The spin only makes sure that a woken thread
// which runs before its waker's next line has recorded its step before the change. Cases, each at
// 0x70 and at 0xA0:
// - mask 0x10000, core 0 alone, which the caller already has
// - mask 0x30000, cores 0 and 1
// - mask 0x70000, all user cores
// - mask 0, which means any user core
//
// Each case prints the result of the change, which is the old mask, and the steps in the order
// they were recorded.
//
// Then a change from another core. S spins on core 0 without syscalls with R ready behind it, and
// the main thread, on core 1, changes S's mask to core 0 alone, waits 300 ms and notes whether R
// ran and S's status. Then it stops S, and R runs once S has ended. Cases, at 0x70 and at 0xA0.
// Each prints the result of the change, whether R ran within 300 ms of it, and S's status then.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAX_STEPS 4
// Loops of the spin at most, a woken thread that runs on another core ends it at once
#define SPINS 20000000
// Loops of the spinner at most, far past the 300 ms the main thread waits
#define REMOTE_SPINS 400000000

static SceUID go_sema, done_sema, other, self;
static volatile int change_result, other_ran;
static int step_count, steps[MAX_STEPS];

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < MAX_STEPS)
        steps[i] = step;
}

// Waits until S signals it, records 1, then signals S
static int other_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(go_sema, 1, &timeout);
    record(1);
    other_ran = 1;
    sceKernelSignalSema(done_sema, 1);
    return 0;
}

// Records 0, readies R, spins until R ran, changes its own mask to the one it is passed, records 2
static int self_entry(SceSize args, void *argp) {
    (void)args;
    const int mask = *(const int *)argp;
    record(0);
    sceKernelSignalSema(go_sema, 1);
    for (volatile int n = 0; n < SPINS && !other_ran; n++)
        ;
    change_result = sceKernelChangeThreadCpuAffinityMask(0, mask);
    record(2);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(done_sema, 1, &timeout);
    return 0;
}

// Waits for a thread to end, for a few seconds at most, and deletes it
static void join(SceUID thread) {
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);
}

static void print_steps(const char *name) {
    const int count = step_count < MAX_STEPS ? step_count : MAX_STEPS;
    PRINTF(step_count, "%s: steps recorded", name);
    for (int i = 0; i < count; i++)
        PRINTF(steps[i], "%s: step %d", name, i);
}

// Runs S with the mask, with R ready behind it at the same priority, and prints the steps
static void run_case(int priority, int mask) {
    char name[64];
    sceClibSnprintf(name, sizeof(name), "at 0x%X, mask 0x%X", priority, mask);
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = -1;
    change_result = -1;
    other_ran = 0;
    go_sema = sceKernelCreateSema("go", 0, 0, 1, NULL);
    done_sema = sceKernelCreateSema("done", 0, 0, 1, NULL);

    other = sceKernelCreateThread("other", other_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(other, 0, NULL);
    test_wait_until_waiting(other);
    self = sceKernelCreateThread("self", self_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(self, sizeof(mask), &mask);

    // Joined first, so every record the threads made is complete
    join(self);
    join(other);
    sceKernelDeleteSema(go_sema);
    sceKernelDeleteSema(done_sema);

    PRINTF(change_result, "%s: the change", name);
    print_steps(name);
}

static volatile int s_up, s_stop, r_ran;
static volatile SceUID r_uid;
static int r_priority;

// Records that R ran
static int r_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    r_ran = 1;
    return 0;
}

// Creates R, masked to core 0 at the same priority, starts it so it is ready behind this thread,
// then spins until stopped
static int s_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    r_uid = sceKernelCreateThread("r", r_entry, r_priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(r_uid, 0, NULL);
    s_up = 1;
    for (volatile int n = 0; n < REMOTE_SPINS && !s_stop; n++)
        ;
    return 0;
}

// Changes a spinning S to core 0 alone from core 1, with R ready behind it, and prints what it sees
static void run_remote(int priority) {
    char name[64];
    sceClibSnprintf(name, sizeof(name), "remote, at 0x%X", priority);
    s_up = s_stop = r_ran = 0;
    r_priority = priority;
    const SceUID s = sceKernelCreateThread("s", s_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(s, 0, NULL);
    TEST_WAIT_FOR(s_up);
    const int result = sceKernelChangeThreadCpuAffinityMask(s, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelDelayThread(300000);
    const int ran = r_ran;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(s, &info);
    s_stop = 1;
    join(s);
    // Once S has ended R is the only thread left on its core, so it runs
    join(r_uid);
    PRINTF(result, "%s: the change", name);
    PRINTF(ran, "%s: R ran within 300 ms of the change", name);
    PRINTF(info.status, "%s: S's status then", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case(0x70, SCE_KERNEL_CPU_MASK_USER_0);
    run_case(0x70, SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_1);
    run_case(0x70, SCE_KERNEL_CPU_MASK_USER_ALL);
    run_case(0x70, 0);
    run_case(0xA0, SCE_KERNEL_CPU_MASK_USER_0);
    run_case(0xA0, SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_1);
    run_case(0xA0, SCE_KERNEL_CPU_MASK_USER_ALL);
    run_case(0xA0, 0);

    run_remote(0x70);
    run_remote(0xA0);

    test_exit();
    return 0;
}
