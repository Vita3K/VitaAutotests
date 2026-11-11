// The CPU change count of thread info for a thread that never ran, after a run pinned to one core,
// after a second run on the same core, and after a mask change while it was dormant, which the next
// start resets. A second thread waits pinned to core 1, gets the mask of core 2 while it waits and
// is woken, so it runs on two cores in one run. Every read happens while the thread is not started
// or has ended. The count is printed as a comparison with 0xFFFFFFFF (never ran), 0 and 1.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static int idle_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

static SceUInt32 change_count(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.changeCpuCount;
}

static SceUID gate;

static int gated_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// Starts the thread, waits for its end and returns its change count
static SceUInt32 run_once(SceUID thread) {
    sceKernelStartThread(thread, 0, NULL);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    return change_count(thread);
}

int main(void) {
    const SceUID pinned = sceKernelCreateThread("pinned", idle_entry, 0x60, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    const SceUID unpinned = sceKernelCreateThread("unpinned", idle_entry, 0x60, 0x4000, 0, 0, NULL);
    const SceUInt32 pinned_created = change_count(pinned);
    const SceUInt32 unpinned_created = change_count(unpinned);
    const SceUInt32 pinned_first = run_once(pinned);
    const SceUInt32 pinned_second = run_once(pinned);
    const int moved = sceKernelChangeThreadCpuAffinityMask(pinned, SCE_KERNEL_CPU_MASK_USER_2);
    const SceUInt32 pinned_moved = run_once(pinned);

    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    const SceUID mover = sceKernelCreateThread("mover", gated_entry, 0x60, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(mover, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(mover, WAIT_TYPE_SEMA, gate));
    const int waiting_moved = sceKernelChangeThreadCpuAffinityMask(mover, SCE_KERNEL_CPU_MASK_USER_2);
    sceKernelSignalSema(gate, 1);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(mover, NULL, &timeout);
    const SceUInt32 mover_count = change_count(mover);

    PRINT("pinned thread created: the count is 0xFFFFFFFF", pinned_created == 0xFFFFFFFF);
    PRINT("unpinned thread created: the count is 0xFFFFFFFF", unpinned_created == 0xFFFFFFFF);
    PRINT("pinned thread after its first run: the count is 0", pinned_first == 0);
    PRINT("pinned thread after its second run: the count is 0", pinned_second == 0);
    PRINT("give it the mask of core 2 while dormant: the call is a success", moved >= 0);
    PRINT("pinned thread after its next run, whose start resets the mask: the count is 0", pinned_moved == 0);
    PRINT("give a waiting thread the mask of core 2: the call is a success", waiting_moved >= 0);
    PRINT("that thread after it woke and ended: the count is 1", mover_count == 1);

    sceKernelDeleteThread(pinned);
    sceKernelDeleteThread(unpinned);
    sceKernelDeleteThread(mover);
    sceKernelDeleteSema(gate);
    test_exit();
    return 0;
}
