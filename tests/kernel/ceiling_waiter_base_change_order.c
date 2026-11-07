// Where a priority change puts a waiter in a priority queue when a mutex ceiling it holds keeps it
// above its new priority. W, on core 0, locks a mutex with the ceiling 0x80 and waits on a semaphore
// that orders its waiters by priority. X, at 0x80 on core 0, waits on it next. The main thread, on
// core 1 at 0x60, prints both threads' priorities, changes W's priority, prints W's priority again,
// then signals the semaphore once and prints which thread its signal woke first, then signals it
// again for the other. The cases:
// - W at 0x70, above the ceiling, changed to 0xB0, below it
// - W at 0x70 changed to 0x80, the ceiling itself
// - W at 0x70 changed to its own priority
// - W at 0xB0, which the ceiling raises to 0x80, changed to 0xB8, still below the ceiling
// - W at 0xB0, raised to 0x80, changed to 0xB0, its own priority
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID sema, ceiling;
static volatile int w_waiting, order_count, order[2];

static void woke(int who) {
    const int i = __atomic_fetch_add(&order_count, 1, __ATOMIC_SEQ_CST);
    if (i < 2)
        order[i] = who;
}

static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(ceiling, 1, NULL);
    w_waiting = 1;
    SceUInt timeout = 10 * TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    woke(1);
    sceKernelUnlockMutex(ceiling, 1);
    return 0;
}

static int x_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 10 * TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    woke(2);
    return 0;
}

static int priority_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.currentPriority;
}

static void run(const char *name, int w_priority, int new_priority) {
    order_count = 0;
    order[0] = order[1] = 0;
    w_waiting = 0;
    sema = sceKernelCreateSema("sema", SCE_KERNEL_ATTR_THREAD_PRIO, 0, 2, NULL);
    SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = 0x80 };
    ceiling = sceKernelCreateMutex("ceiling", SCE_KERNEL_MUTEX_ATTR_CEILING, 0, &opt);
    const SceUID w = sceKernelCreateThread("w", w_entry, w_priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID x = sceKernelCreateThread("x", x_entry, 0x80, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(w_waiting && test_in_wait(w, WAIT_TYPE_SEMA, sema));
    sceKernelStartThread(x, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(x, WAIT_TYPE_SEMA, sema));
    PRINTF(priority_of(w), "%s: W's priority while it waits", name);
    PRINTF(priority_of(x), "%s: X's priority while it waits", name);
    PRINTF(sceKernelChangeThreadPriority(w, new_priority), "%s: the change of W to 0x%X", name, new_priority);
    PRINTF(priority_of(w), "%s: W's priority after the change", name);
    sceKernelSignalSema(sema, 1);
    TEST_WAIT_FOR(order_count >= 1);
    sceKernelSignalSema(sema, 1);
    // Both threads wrote their entries once they ended
    test_join_thread(w);
    test_join_thread(x);
    PRINTF(order[0], "%s: the first woken (1 is W, 2 is X)", name);
    PRINTF(order[1], "%s: the second woken", name);
    sceKernelDeleteMutex(ceiling);
    sceKernelDeleteSema(sema);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    sceKernelChangeThreadPriority(0, 0x60);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    run("W at 0x70 changed to 0xB0", 0x70, 0xB0);
    run("W at 0x70 changed to 0x80", 0x70, 0x80);
    run("W at 0x70 changed to 0x70", 0x70, 0x70);
    run("W at 0xB0 changed to 0xB8", 0xB0, 0xB8);
    run("W at 0xB0 changed to 0xB0", 0xB0, 0xB0);

    test_exit();
    return 0;
}
