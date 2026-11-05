// Whether the core a started thread gets, when no core is a better choice, walks down one core at
// a time from the core of the previous start, wrapping around the three cores of the mask. Each
// core runs a thread at 0x48: the main thread on core 0 and spinner threads, which loop without
// syscalls, on cores 1 and 2. Twelve threads at 0x48 that never ran, with a mask of cores 0 to 2,
// are started back to back, so no core runs a lower priority and none holds the thread's last
// core. The starter reads the thread's current CPU as soon as its start returns, which is the
// core it was queued on. Nothing prints until every start is done. The starts are:
// - 0 to 5: made by the main thread
// - 6 to 11: made by the spinners on cores 1 and 2 in turn, at the main thread's request, while it
//   spins without syscalls so that core 0 stays busy
// Each start after the first prints whether its core is the previous start's core minus 1, modulo
// 3. The core of the first start isn't printed, since it depends on the starts before this test.
// Starts 6 to 11 continue the walk of the first six only if one walk is shared by every starting
// core.
//
// On a PS Vita TV on firmware 3.65 every start after the first gets the core of the previous
// start minus 1, modulo 3, whether the main thread or a spinner on core 1 or 2 made it. So one
// walk is shared by every starting core.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define PRIORITY 0x48
#define STARTS 12
#define MAIN_STARTS 6
#define SPINS 400000000
// How long the main thread spins for a spinner to start a thread, far longer than that takes
#define REQUEST_SPINS 100000000

static SceUID threads[STARTS], spinners[3];
static volatile int spinner_up[3], spinner_stop, request, request_start, ran;
static int cores[STARTS];

// Starts thread i and records the core it was queued on
static void start_thread(int i) {
    sceKernelStartThread(threads[i], 0, NULL);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(threads[i], &info);
    cores[i] = info.currentCpuId;
}

// Holds its core, and starts a thread when the main thread asks this core to
static int spinner_entry(SceSize args, void *argp) {
    (void)args;
    const int core = *(const int *)argp;
    spinner_up[core] = 1;
    for (volatile int n = 0; n < SPINS && !spinner_stop; n++) {
        if (request == core + 1) {
            start_thread(request_start);
            request = 0;
        }
    }
    return 0;
}

static int thread_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    __atomic_fetch_add(&ran, 1, __ATOMIC_SEQ_CST);
    return 0;
}

// Waits for a thread to end, for at most 2 seconds, and deletes it
static void end_thread(SceUID thread) {
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    for (int i = 0; i < STARTS; i++) {
        threads[i] = sceKernelCreateThread("t", thread_entry, PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2, NULL);
        cores[i] = -2;
    }
    for (int core = 1; core < 3; core++) {
        spinners[core] = sceKernelCreateThread("spinner", spinner_entry, PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0 << core, NULL);
        sceKernelStartThread(spinners[core], sizeof(core), &core);
        TEST_WAIT_FOR(spinner_up[core]);
    }

    // The main thread must not wait from here on, or core 0 would be idle and the best choice
    for (int i = 0; i < MAIN_STARTS; i++)
        start_thread(i);
    for (int i = MAIN_STARTS; i < STARTS; i++) {
        request_start = i;
        // The spinner of core 1 or 2, which waits for request == core + 1
        request = 2 + i % 2;
        for (int n = 0; n < REQUEST_SPINS && request; n++)
            ;
    }

    spinner_stop = 1;
    TEST_WAIT_FOR(ran == STARTS);
    for (int core = 1; core < 3; core++)
        end_thread(spinners[core]);
    for (int i = 0; i < STARTS; i++)
        end_thread(threads[i]);

    for (int i = 1; i < STARTS; i++)
        PRINTF(cores[i - 1] >= 0 && cores[i] == (cores[i - 1] + 2) % 3, "start %d by %s: its core is the previous core minus 1", i, i < MAIN_STARTS ? "the main thread" : "a spinner");
    test_exit();
    return 0;
}
