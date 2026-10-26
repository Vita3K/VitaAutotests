// The CPU affinity fields of a thread created with mask 0, and of one created with all three user
// cores (0x70000), read from outside the thread and from inside it. The thread's first run waits
// on a semaphore, so the outside reads are taken while it waits. Between the reads the thread's
// mask is changed to core 1 and then to 0, and the thread is ended. Then it is started again, and
// the fields are read again from inside, and from outside once it ended again. The main thread is
// pinned on core 2 and only waits, so it stays off the cores that the test threads are moved to.
// Where a thread ran is printed only as a comparison, since a mask of 0 or 0x70000 lets it run on
// any of the three user cores.
// The cases: a thread with mask 0, then a thread with mask 0x70000. Each one is read dormant, while
// it waits, after the change to core 1, after the change to 0, from inside its first run, and from
// inside its restarted run.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// The thread's first run waits on this until the main thread has read its fields from outside
static SceUID park;
// Whether the next run waits on park, which the first run does and the restarted run doesn't
static volatile int hold;

// The affinity fields a run reads about itself
typedef struct Fields {
    SceInt32 init;
    SceInt32 current;
    SceInt32 get;
    SceInt32 get2;
    SceInt32 cpu;
} Fields;

static Fields inside;

// Reads its own affinity fields, then waits on park in the first run
static int zero_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    inside.init = info.initCpuAffinityMask;
    inside.current = info.currentCpuAffinityMask;
    inside.get = sceKernelGetThreadCpuAffinityMask(0);
    inside.get2 = sceKernelGetThreadCpuAffinityMask2(0);
    inside.cpu = sceKernelGetCpuId();
    if (hold) {
        SceUInt timeout = TEST_WAIT_US;
        sceKernelWaitSema(park, 1, &timeout);
    }
    return 0;
}

// Prints the affinity fields of thread as another thread reads them
static void print_outside(const char *name, const char *when, SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    PRINTF(sceKernelGetThreadInfo(thread, &info), "%s, %s, outside: info", name, when);
    PRINTF(info.initCpuAffinityMask, "%s, %s, outside: the initial mask", name, when);
    PRINTF(info.currentCpuAffinityMask, "%s, %s, outside: the current mask", name, when);
    PRINTF(sceKernelGetThreadCpuAffinityMask(thread), "%s, %s, outside: GetThreadCpuAffinityMask", name, when);
    PRINTF(sceKernelGetThreadCpuAffinityMask2(thread), "%s, %s, outside: GetThreadCpuAffinityMask2", name, when);
}

// Prints the affinity fields that the last run read about itself
static void print_inside(const char *name, const char *run) {
    PRINTF(inside.init, "%s, %s, inside: the initial mask", name, run);
    PRINTF(inside.current, "%s, %s, inside: the current mask", name, run);
    PRINTF(inside.get, "%s, %s, inside: GetThreadCpuAffinityMask", name, run);
    PRINTF(inside.get2, "%s, %s, inside: GetThreadCpuAffinityMask2", name, run);
    PRINTF(inside.cpu >= 0 && inside.cpu <= 2, "%s, %s, inside: it ran on a user core", name, run);
}

// Creates a thread with mask, and reads its fields through the changes and a restart
static void mask_case(const char *name, int mask) {
    hold = 1;
    const SceUID thread = sceKernelCreateThread("zero", zero_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, mask, NULL);
    print_outside(name, "dormant", thread);

    sceKernelStartThread(thread, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_SEMA, park));
    print_inside(name, "first run");
    print_outside(name, "waiting", thread);

    PRINTF(sceKernelChangeThreadCpuAffinityMask(thread, SCE_KERNEL_CPU_MASK_USER_1), "%s, change to core 1: result", name);
    print_outside(name, "on core 1", thread);
    PRINTF(sceKernelChangeThreadCpuAffinityMask(thread, 0), "%s, change to 0: result", name);
    print_outside(name, "changed to 0", thread);

    sceKernelSignalSema(park, 1);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    hold = 0;
    PRINTF(sceKernelStartThread(thread, 0, NULL), "%s, restart: result", name);
    timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    print_inside(name, "restarted run");
    print_outside(name, "ended after the restart", thread);

    sceKernelDeleteThread(thread);
}

int main(void) {
    const int orig_mask = sceKernelGetThreadCpuAffinityMask(0);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    TEST_WAIT_FOR(sceKernelGetCpuId() == 2);

    park = sceKernelCreateSema("park", 0, 0, 1, NULL);
    mask_case("mask 0", 0);
    mask_case("mask 0x70000", SCE_KERNEL_CPU_MASK_USER_ALL);
    sceKernelDeleteSema(park);

    sceKernelChangeThreadCpuAffinityMask(0, orig_mask);
    test_exit();
    return 0;
}
