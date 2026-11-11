// Which preempt counter of thread info grows when a thread wakes a higher priority thread on its
// own core through a simple event, and through a semaphore. T runs at 0x80 on core 0 and H at 0x60
// on core 0. H waits on the event or the semaphore and counts each wake. T reads its own counters,
// sets the event ten times, reads them again, and does the same with ten semaphore signals. The
// main thread waits on core 2. T checks that H waits before each set or signal. The test prints the
// change of T's interrupt preempt count and of its thread preempt count over each ten, and whether
// H woke ten times.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define T_PRIORITY 0x80
#define H_PRIORITY 0x60
#define ROUNDS 10

static SceUID event, sema, go, h_thread;
static volatile int woke_event, woke_sema, h_quit;
static int event_intr, event_preempt, sema_intr, sema_preempt;

// Waits for the event or the semaphore in turn and counts each wake
static int h_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    while (!h_quit) {
        SceUInt timeout = 3 * TEST_WAIT_US;
        SceUInt32 pattern;
        if (sceKernelWaitEvent(event, EVENT_BIT_A, &pattern, NULL, &timeout) == 0 && !h_quit)
            woke_event++;
        if (woke_event == ROUNDS)
            break;
    }
    while (!h_quit) {
        SceUInt timeout = 3 * TEST_WAIT_US;
        if (sceKernelWaitSema(sema, 1, &timeout) == 0 && !h_quit)
            woke_sema++;
    }
    return 0;
}

static void read_counters(SceUInt32 *intr, SceUInt32 *preempt) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    *intr = info.intrPreemptCount;
    *preempt = info.threadPreemptCount;
}

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelWaitSema(go, 1, NULL);
    SceUInt32 intr0, preempt0, intr1, preempt1;

    read_counters(&intr0, &preempt0);
    for (int i = 0; i < ROUNDS; i++) {
        TEST_WAIT_FOR(test_in_wait(h_thread, WAIT_TYPE_EVENT, event));
        sceKernelSetEvent(event, EVENT_BIT_A, 0);
    }
    read_counters(&intr1, &preempt1);
    event_intr = (int)(intr1 - intr0);
    event_preempt = (int)(preempt1 - preempt0);

    read_counters(&intr0, &preempt0);
    for (int i = 0; i < ROUNDS; i++) {
        TEST_WAIT_FOR(test_in_wait(h_thread, WAIT_TYPE_SEMA, sema));
        sceKernelSignalSema(sema, 1);
    }
    read_counters(&intr1, &preempt1);
    sema_intr = (int)(intr1 - intr0);
    sema_preempt = (int)(preempt1 - preempt0);
    return 0;
}

int main(void) {
    const int orig_mask = sceKernelGetThreadCpuAffinityMask(0);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    TEST_WAIT_FOR(sceKernelGetCpuId() == 2);

    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL);
    sema = sceKernelCreateSema("sema", 0, 0, ROUNDS, NULL);
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    const SceUID h = sceKernelCreateThread("h", h_entry, H_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID t = sceKernelCreateThread("t", t_entry, T_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    h_thread = h;
    sceKernelStartThread(h, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(h, WAIT_TYPE_EVENT, event));
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, go));
    sceKernelSignalSema(go, 1);
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(t, NULL, &timeout);

    TEST_WAIT_FOR(woke_event == ROUNDS && woke_sema == ROUNDS);
    PRINT("ten event sets: H woke ten times", woke_event == ROUNDS);
    PRINT("ten event sets: the interrupt preempt count of T grew by", event_intr);
    PRINT("ten event sets: the thread preempt count of T grew by", event_preempt);
    PRINT("ten semaphore signals: H woke ten times", woke_sema == ROUNDS);
    PRINT("ten semaphore signals: the interrupt preempt count of T grew by", sema_intr);
    PRINT("ten semaphore signals: the thread preempt count of T grew by", sema_preempt);

    h_quit = 1;
    sceKernelSignalSema(sema, 1);
    sceKernelSetEvent(event, EVENT_BIT_A, 0);
    timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(h, NULL, &timeout);
    sceKernelDeleteThread(h);
    sceKernelDeleteThread(t);
    sceKernelDeleteSimpleEvent(event);
    sceKernelDeleteSema(sema);
    sceKernelDeleteSema(go);
    sceKernelChangeThreadCpuAffinityMask(0, orig_mask);
    test_exit();
    return 0;
}
