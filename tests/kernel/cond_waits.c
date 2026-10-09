// Condition variable waits: the mutex during and after the wait, and who a signal wakes
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define LOW_PRIORITY 0x10000102
#define HIGH_PRIORITY 0x100000FE

static SceUID mutex, cond;
static int wait_result[2], order[2], woken;
static int unlock_two, unlock_three;

// Locks the recursive mutex twice, waits, then unlocks twice and once more
static int recursive_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 2, NULL);
    SceUInt timeout = 500000;
    wait_result[0] = sceKernelWaitCond(cond, &timeout);
    unlock_two = sceKernelUnlockMutex(mutex, 2);
    unlock_three = sceKernelUnlockMutex(mutex, 1);
    return 0;
}

// Waits on the condition into slot i
static int wait_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    SceUInt timeout = TEST_WAIT_US;
    wait_result[i] = sceKernelWaitCond(cond, &timeout);
    if (wait_result[i] == 0)
        order[i] = ++woken;
    sceKernelUnlockMutex(mutex, 1);
    return 0;
}

static SceUID start_waiter(int i, int priority) {
    wait_result[i] = -1;
    order[i] = 0;
    const SceUID thread = sceKernelCreateThread("waiter", wait_thread, priority, 0x4000, 0, 0, NULL);
    sceKernelStartThread(thread, sizeof(i), &i);
    test_wait_until_waiting(thread);
    return thread;
}

int main(void) {
    // The wait unlocks the mutex once, so a mutex locked twice stays owned during it
    mutex = sceKernelCreateMutex("recursive", MUTEX_ATTR_RECURSIVE, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    SceUID waiter = test_start_thread(recursive_thread);
    test_wait_until_waiting(waiter);
    PRINT("a mutex locked twice stays owned during the wait", sceKernelTryLockMutex(mutex, 1));
    sceKernelSignalCond(cond);
    test_join_thread(waiter);
    PRINT("the wait ends", wait_result[0]);
    PRINT("after it the mutex is locked twice again", unlock_two);
    PRINT("and not more", unlock_three);

    // A woken waiter waits for the mutex while another thread holds it
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    waiter = start_waiter(0, 0x10000100);
    sceKernelLockMutex(mutex, 1, NULL);
    sceKernelSignalCond(cond);
    test_let_threads_wait();
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(waiter, &info);
    PRINT("a signaled waiter waits for the mutex", info.waitType);
    PRINT("the wait isn't over before it has the mutex", wait_result[0] == -1);
    sceKernelUnlockMutex(mutex, 1);
    test_join_thread(waiter);
    PRINT("it ends once the mutex is free", wait_result[0]);

    // Signaling a thread wakes only that one
    woken = 0;
    const SceUID first = start_waiter(0, 0x10000100);
    const SceUID second = start_waiter(1, 0x10000100);
    PRINT("signal the second waiter", sceKernelSignalCondTo(cond, second));
    test_let_threads_wait();
    PRINT("the second waiter woke", order[1]);
    PRINT("the first one still waits", order[0]);
    PRINT("signal a thread that isn't waiting", sceKernelSignalCondTo(cond, sceKernelGetThreadId()) < 0);
    sceKernelSignalCond(cond);
    test_join_thread(first);
    test_join_thread(second);

    // A signal wakes one waiter, the higher priority one with the priority attribute
    cond = sceKernelCreateCond("prio", ATTR_TH_PRIO, mutex, NULL);
    woken = 0;
    const SceUID low = start_waiter(0, LOW_PRIORITY);
    const SceUID high = start_waiter(1, HIGH_PRIORITY);
    sceKernelSignalCond(cond);
    test_let_threads_wait();
    PRINT("one signal wakes one of two waiters", (order[0] != 0) + (order[1] != 0));
    PRINT("the higher priority waiter is the one", order[1] != 0);
    sceKernelSignalCond(cond);
    test_join_thread(low);
    test_join_thread(high);
    PRINT("the next signal wakes the other", order[0] != 0);

    test_exit();
    return 0;
}
