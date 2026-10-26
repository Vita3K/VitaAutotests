// Which priority and affinity a thread starts again with after its priority and affinity were
// changed, and what sceKernelGetThreadInfo shows for them before the restart. The thread is created
// at priority 0x60 on core 1. The cases: the changes made while it is dormant after its first run
// ended, and the changes made while it waits on a semaphore during its first run. In both, the
// info shows the changes until the restart, and the restarted run starts with the create values.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CREATE_PRIORITY 0x60
#define CREATE_AFFINITY SCE_KERNEL_CPU_MASK_USER_1
#define CHANGED_PRIORITY 0x70
#define CHANGED_AFFINITY SCE_KERNEL_CPU_MASK_USER_2

static SceUID go;
static volatile int runs, run_priority, run_affinity, wait_in_first_run;

// Records the priority and affinity it runs with, and in its first run waits for go when asked to
static int affinity_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    runs++;
    run_priority = sceKernelGetThreadCurrentPriority();
    run_affinity = sceKernelGetThreadCpuAffinityMask(0);
    if (runs == 1 && wait_in_first_run) {
        SceUInt timeout = TEST_WAIT_US;
        sceKernelWaitSema(go, 1, &timeout);
    }
    return 0;
}

static SceUID create_thread(void) {
    return sceKernelCreateThread("restart", affinity_thread, CREATE_PRIORITY, 0x4000, 0, CREATE_AFFINITY, NULL);
}

// Prints the priorities and affinities the info reports for thread
static void print_info(const char *name, SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    PRINTF(sceKernelGetThreadInfo(thread, &info), "%s: info", name);
    PRINTF(info.initPriority, "%s: the initial priority", name);
    PRINTF(info.currentPriority, "%s: the current priority", name);
    PRINTF(info.initCpuAffinityMask, "%s: the initial affinity", name);
    PRINTF(info.currentCpuAffinityMask, "%s: the current affinity", name);
}

// Changes made while the thread is dormant, after its first run ended
static void dormant_changes(void) {
    runs = 0;
    wait_in_first_run = 0;
    const SceUID thread = create_thread();
    sceKernelStartThread(thread, 0, NULL);
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    PRINT("dormant: the first run's priority", run_priority);
    PRINT("dormant: the first run's affinity", run_affinity);
    PRINT("dormant: change the priority", sceKernelChangeThreadPriority(thread, CHANGED_PRIORITY));
    PRINT("dormant: change the affinity", sceKernelChangeThreadCpuAffinityMask(thread, CHANGED_AFFINITY));
    print_info("dormant, changed", thread);
    PRINT("dormant: start it again", sceKernelStartThread(thread, 0, NULL));
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    PRINT("dormant: restarted run: its priority", run_priority);
    PRINT("dormant: restarted run: its affinity", run_affinity);
    sceKernelDeleteThread(thread);
}

// Changes made while the thread waits in its first run, read before and after the restart
static void waiting_changes(void) {
    runs = 0;
    wait_in_first_run = 1;
    const SceUID thread = create_thread();
    sceKernelStartThread(thread, 0, NULL);
    test_wait_until_waiting(thread);
    PRINT("waiting: its first run's priority", run_priority);
    PRINT("waiting: its first run's affinity", run_affinity);
    PRINT("waiting: change the priority", sceKernelChangeThreadPriority(thread, CHANGED_PRIORITY));
    PRINT("waiting: change the affinity", sceKernelChangeThreadCpuAffinityMask(thread, CHANGED_AFFINITY));
    print_info("waiting, changed", thread);
    sceKernelSignalSema(go, 1);
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    print_info("ended, changed while waiting", thread);
    PRINT("restart it", sceKernelStartThread(thread, 0, NULL));
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    PRINT("waiting: restarted run: its priority", run_priority);
    PRINT("waiting: restarted run: its affinity", run_affinity);
    sceKernelDeleteThread(thread);
}

int main(void) {
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    dormant_changes();
    waiting_changes();

    test_exit();
    return 0;
}
