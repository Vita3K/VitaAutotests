// How much each counter of thread info grows over a ForVM suspend of a thread that spins in user
// code. The test thread T has the VM attribute and spins alone on core 0 in a counted loop, without
// any system call. The main thread runs on core 1. It reads T's counters, suspends T, waits until T
// shows as suspended, and reads them again. After each resume it waits until T makes progress, so
// the next suspend finds it running user code. Three rounds. Each prints the suspend's result, and
// the change of the interrupt preempt, thread preempt and release counts, and of the CPU change
// count, across the suspend.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define ROUNDS 3
// The status bit of a thread suspended for ForVM
#define STATUS_SUSPENDED 0x100

static volatile int t_stop;
static volatile unsigned t_progress;

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    while (!t_stop)
        t_progress++;
    return 0;
}

static void read_info(SceUID thread, SceKernelThreadInfo *info) {
    info->size = sizeof(*info);
    sceKernelGetThreadInfo(thread, info);
}

static int is_suspended(SceUID thread) {
    SceKernelThreadInfo info;
    read_info(thread, &info);
    return (info.status & STATUS_SUSPENDED) != 0;
}

int main(void) {
    const int orig_mask = sceKernelGetThreadCpuAffinityMask(0);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    TEST_WAIT_FOR(sceKernelGetCpuId() == 1);

    const SceUID t = sceKernelCreateThread("t", t_entry, 0xA0, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(t_progress != 0);

    int suspend[ROUNDS], intr[ROUNDS], preempt[ROUNDS], release[ROUNDS], change[ROUNDS];
    for (int i = 0; i < ROUNDS; i++) {
        SceKernelThreadInfo before, during;
        read_info(t, &before);
        suspend[i] = sceKernelSuspendThreadForVM(t);
        TEST_WAIT_FOR(is_suspended(t));
        read_info(t, &during);
        intr[i] = (int)(during.intrPreemptCount - before.intrPreemptCount);
        preempt[i] = (int)(during.threadPreemptCount - before.threadPreemptCount);
        release[i] = (int)(during.threadReleaseCount - before.threadReleaseCount);
        change[i] = (int)(during.changeCpuCount - before.changeCpuCount);
        sceKernelResumeThreadForVM(t);
        const unsigned seen = t_progress;
        TEST_WAIT_FOR(t_progress != seen);
    }

    for (int i = 0; i < ROUNDS; i++) {
        PRINTF(suspend[i], "round %d: suspend: result", i);
        PRINTF(intr[i], "round %d: the interrupt preempt count grew by", i);
        PRINTF(preempt[i], "round %d: the thread preempt count grew by", i);
        PRINTF(release[i], "round %d: the release count grew by", i);
        PRINTF(change[i], "round %d: the CPU change count grew by", i);
    }

    t_stop = 1;
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(t, NULL, &timeout);
    sceKernelDeleteThread(t);
    sceKernelChangeThreadCpuAffinityMask(0, orig_mask);
    test_exit();
    return 0;
}
