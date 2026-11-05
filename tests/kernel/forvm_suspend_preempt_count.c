// Whether a ForVM suspend of a thread that spins in user code counts as an interrupt preemption in
// the thread's info. The test thread T has the VM attribute and spins alone on core 0 without any
// system call, so it is running user code when it is suspended. The main thread runs on core 1. It
// suspends and resumes T three times. Before each suspend it reads T's counters, and it reads them
// again once T shows as suspended. After each resume it waits until T makes progress again, so the
// next suspend finds it running. It prints each suspend's and resume's result, whether the
// interrupt preempt count grew by at least 1 across each suspend, and then whether it grew by at
// least 3 and whether the thread preempt count stayed the same over the three rounds.
//
// On a PS Vita TV on firmware 3.65 every suspend returns 1 and every resume returns 0. The
// interrupt preempt count grows by at least 1 across each suspend and by at least 3 over the three
// rounds, the thread preempt count stays the same, and T makes progress after each resume.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define ROUNDS 3
// The status bit of a thread suspended for ForVM
#define STATUS_SUSPENDED 0x100

static volatile int t_stop;
static volatile unsigned t_progress;

// Spins without any system call until the main thread stops it
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    while (!t_stop)
        t_progress++;
    return 0;
}

// Reads the counters of thread info
static void read_info(SceUID thread, SceKernelThreadInfo *info) {
    info->size = sizeof(*info);
    sceKernelGetThreadInfo(thread, info);
}

// Returns whether thread is suspended for ForVM
static int is_suspended(SceUID thread) {
    SceKernelThreadInfo info;
    read_info(thread, &info);
    return (info.status & STATUS_SUSPENDED) != 0;
}

int main(void) {
    const int orig_mask = sceKernelGetThreadCpuAffinityMask(0);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    TEST_WAIT_FOR(sceKernelGetCpuId() == 1);

    const SceUID t = sceKernelCreateThread("t", t_entry, 0xA0, 0x4000, THREAD_ATTR_VM,
        SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(t_progress != 0);

    SceKernelThreadInfo first, before, during;
    read_info(t, &first);
    for (int i = 0; i < ROUNDS; i++) {
        read_info(t, &before);
        PRINTF(sceKernelSuspendThreadForVM(t), "round %d: suspend: result", i);
        TEST_WAIT_FOR(is_suspended(t));
        read_info(t, &during);
        PRINTF((int)(during.intrPreemptCount - before.intrPreemptCount) >= 1,
            "round %d: the interrupt preempt count grew by at least 1", i);
        PRINTF(sceKernelResumeThreadForVM(t), "round %d: resume: result", i);
        const unsigned seen = t_progress;
        TEST_WAIT_FOR(t_progress != seen);
        PRINTF(t_progress != seen, "round %d: the thread made progress after the resume", i);
    }
    read_info(t, &during);
    PRINT("after 3 rounds: the interrupt preempt count grew by at least 3",
        (int)(during.intrPreemptCount - first.intrPreemptCount) >= ROUNDS);
    PRINT("after 3 rounds: the thread preempt count did not grow",
        during.threadPreemptCount == first.threadPreemptCount);

    t_stop = 1;
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(t, NULL, &timeout);
    sceKernelDeleteThread(t);
    sceKernelChangeThreadCpuAffinityMask(0, orig_mask);
    test_exit();
    return 0;
}
