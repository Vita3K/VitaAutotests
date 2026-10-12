// Where a waiting thread goes in a priority queue when its priority changes while it waits
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define LOW_PRIORITY 0x10000102
#define MID_PRIORITY 0x10000100

static SceUID sema;
static int order[3], woken;

static int sema_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelWaitSema(sema, 1, &timeout) == 0)
        order[i] = ++woken;
    return 0;
}

static SceUID start(int i, int priority) {
    const SceUID thread = sceKernelCreateThread("waiter", sema_thread, priority, 0x4000, 0, 0, NULL);
    sceKernelStartThread(thread, sizeof(i), &i);
    test_wait_until_waiting(thread);
    return thread;
}

// Wakes the waiters one at a time and joins them
static void wake_all(SceUID *threads, int count) {
    for (int i = 0; i < count; i++) {
        sceKernelSignalSema(sema, 1);
        test_let_threads_wait();
    }
    for (int i = 0; i < count; i++)
        test_join_thread(threads[i]);
}

int main(void) {
    SceUID threads[3];

    // Two low priority waiters and one that waited after them at a higher priority, then the second
    // low one raised to the higher priority. It wakes third if it stays where it was, second if it
    // goes behind the waiter already at its new priority, and first if it keeps its place by arrival.
    sema = sceKernelCreateSema("raise", ATTR_TH_PRIO, 0, 3, NULL);
    woken = 0;
    threads[0] = start(0, LOW_PRIORITY);
    threads[1] = start(1, LOW_PRIORITY);
    threads[2] = start(2, MID_PRIORITY);
    PRINT("raise the second low priority waiter", sceKernelChangeThreadPriority(threads[1], MID_PRIORITY));
    wake_all(threads, 3);
    PRINT("the low priority waiter that waited first", order[0]);
    PRINT("the raised waiter", order[1]);
    PRINT("the waiter already at that priority", order[2]);
    sceKernelDeleteSema(sema);

    // Changing a waiter's priority to the one it already has
    sema = sceKernelCreateSema("same", ATTR_TH_PRIO, 0, 2, NULL);
    woken = 0;
    threads[0] = start(0, MID_PRIORITY);
    threads[1] = start(1, MID_PRIORITY);
    PRINT("change the first waiter to its own priority", sceKernelChangeThreadPriority(threads[0], MID_PRIORITY));
    wake_all(threads, 2);
    PRINT("the first waiter", order[0]);
    PRINT("the second waiter", order[1]);
    sceKernelDeleteSema(sema);

    test_exit();
    return 0;
}
