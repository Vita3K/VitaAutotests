// Which waiters a signal serves on a semaphore when the first waiter in the queue doesn't fit
// the count. Four waiters queue on a semaphore that starts at 0, in this order: A needs 1, B
// needs 3, C needs 1 and D needs 2. Their priorities are B 0x80, C 0x88, A 0x90 and D 0x98, so
// a priority queue orders them B, C, A, D, and a FIFO queue keeps the order they queued in. A
// signal serves the queue from its head while the head fits the count, and stops at the first
// waiter that doesn't fit, so a waiter behind it that would fit still waits. The test signals 2,
// 1, 1, 5 on a FIFO queue, and 2, 1, 1, 4 on a priority queue. After each signal it waits until
// every waiter is done or still in its wait, then prints the signal's result, the count, the
// number of waiters, and for each waiter whether it is done and whether it still waits. Once the
// queue is empty it prints each waiter's result. The waiters are pinned to core 0 below the main
// thread, as in release_errors_waiters. The model is sema_head_leaves, for its waiters and its
// queue checks.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define SIGNALS 4

enum { WAITER_A, WAITER_B, WAITER_C, WAITER_D, WAITERS };

static const char *const names[WAITERS] = { "A", "B", "C", "D" };
static const int needs[WAITERS] = { 1, 3, 1, 2 };
static const int priorities[WAITERS] = { 0x90, 0x80, 0x88, 0x98 };

static SceUID sema, threads[WAITERS];
static volatile int result[WAITERS], done[WAITERS];

static int waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = 3 * TEST_WAIT_US;
    result[i] = sceKernelWaitSema(sema, needs[i], &timeout);
    done[i] = 1;
    return 0;
}

// Starts the waiters in A, B, C, D order, each once the one before waits
static void start_waiters(SceUInt32 attr) {
    sema = sceKernelCreateSema("sema", attr, 0, 10, NULL);
    for (int i = 0; i < WAITERS; i++) {
        result[i] = 0xDEAD;
        done[i] = 0;
        threads[i] = sceKernelCreateThread(
            "waiter", waiter, priorities[i], 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(threads[i], sizeof(i), &i);
        test_wait_until_waiting(threads[i]);
    }
}

// Whether thread is blocked in a wait on the semaphore. A waiter that a signal served is no longer
// waiting, even before it runs.
static int waits_on_sema(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    if (sceKernelGetThreadInfo(thread, &info) < 0 || !(info.status & SCE_THREAD_WAITING))
        return 0;
    return test_in_wait(thread, WAIT_TYPE_SEMA, sema);
}

// Whether every waiter is done, or still blocked on the semaphore, so the state can be read
static int settled(void) {
    for (int i = 0; i < WAITERS; i++)
        if (!done[i] && !waits_on_sema(threads[i]))
            return 0;
    return 1;
}

// Signals amount, waits until the waiters settle, and prints what the signal left
static void signal_step(const char *name, int amount) {
    const int call = sceKernelSignalSema(sema, amount);
    TEST_WAIT_FOR(settled());
    SceKernelSemaInfo info = { .size = sizeof(info) };
    sceKernelGetSemaInfo(sema, &info);
    PRINTF(call, "%s, signal %d: the call", name, amount);
    PRINTF(info.currentCount, "%s, signal %d: the count", name, amount);
    PRINTF(info.numWaitThreads, "%s, signal %d: the waiters", name, amount);
    for (int i = 0; i < WAITERS; i++) {
        PRINTF(done[i], "%s, signal %d: %s is done", name, amount, names[i]);
        PRINTF(waits_on_sema(threads[i]), "%s, signal %d: %s still waits", name, amount, names[i]);
    }
}

// Queues the four waiters on a semaphore of attr, signals it with each amount in turn, then ends
// the test's threads and prints their results
static void serve_order(SceUInt32 attr, const char *name, const int amounts[SIGNALS]) {
    start_waiters(attr);
    for (int s = 0; s < SIGNALS; s++)
        signal_step(name, amounts[s]);
    sceKernelDeleteSema(sema);
    for (int i = 0; i < WAITERS; i++) {
        test_join_thread(threads[i]);
        PRINTF(result[i], "%s: %s's result", name, names[i]);
    }
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    static const int fifo_amounts[SIGNALS] = { 2, 1, 1, 5 };
    static const int priority_amounts[SIGNALS] = { 2, 1, 1, 4 };
    serve_order(0, "FIFO queue", fifo_amounts);
    serve_order(SCE_KERNEL_ATTR_THREAD_PRIO, "priority queue", priority_amounts);

    test_exit();
    return 0;
}
