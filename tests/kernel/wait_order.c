// Waiters wake in the order they started waiting, or by priority with the priority attribute
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define LOW_PRIORITY 0x10000102
#define HIGH_PRIORITY 0x100000FE

static SceUID sema;
static int woken, order[2];

static int wait_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = 500000;
    if (sceKernelWaitSema(sema, 1, &timeout) == 0)
        order[i] = ++woken;
    return 0;
}

static SceUID start_waiter(int i, int priority) {
    const SceUID thread = sceKernelCreateThread("waiter", wait_thread, priority, 0x4000, 0, 0, NULL);
    sceKernelStartThread(thread, sizeof(i), &i);
    test_wait_until_waiting(thread);
    return thread;
}

// Starts a low priority waiter, then a high priority one, and wakes them one at a time.
// Returns the slot of the waiter that woke first.
static int first_woken(SceUInt32 attr) {
    sema = sceKernelCreateSema("sema", attr, 0, 2, NULL);
    woken = order[0] = order[1] = 0;
    const SceUID low = start_waiter(0, LOW_PRIORITY);
    const SceUID high = start_waiter(1, HIGH_PRIORITY);
    sceKernelSignalSema(sema, 1);
    test_let_threads_wait();
    sceKernelSignalSema(sema, 1);
    test_join_thread(low);
    test_join_thread(high);
    sceKernelDeleteSema(sema);
    return order[0] == 1 ? 0 : order[1] == 1 ? 1 : -1;
}

int main(void) {
    PRINT("a FIFO semaphore wakes the first waiter first", first_woken(0));
    PRINT("a priority semaphore wakes the higher priority waiter first", first_woken(ATTR_TH_PRIO));

    test_exit();
    return 0;
}
