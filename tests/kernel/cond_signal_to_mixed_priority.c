// Which waiters a condition variable with the priority attribute signals when waiters of different
// priorities wait on it, and the order the signaled waiters get their mutex in. Three threads A, B
// and C wait on a condition at priorities 0x90, 0x70 and 0x80, and the condition's mutex is a FIFO
// mutex. The main thread then holds the mutex and makes four calls, and after each one waits for
// the signaled waiters to queue on the mutex: sceKernelSignalCondTo to A, a plain signal, a signal
// to all, and sceKernelSignalCondTo to A again, which is no longer waiting on the condition. After
// each call the test prints its result, the condition's waiting thread count and each waiter's wait
// type. Then the main thread unlocks the mutex, and each waiter records its turn with the mutex as
// one hex digit, A being 1, so the test prints those digits in the order the waiters got the mutex,
// and each waiter's wait result. The models are cond_waits, for the signal to one thread, and
// cond_signal_all_order, for the order.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define WAITERS 3

// Index 0 is A, 1 is B and 2 is C
static const int priorities[WAITERS] = { 0x90, 0x70, 0x80 };
static const char *const names[WAITERS] = { "A", "B", "C" };

static SceUID mutex, cond;
static SceUID threads[WAITERS];
static int results[WAITERS];
static volatile unsigned int order;

// Waits on the condition, then, with the mutex again, records its digit and unlocks the mutex
static int waiter(SceSize args, void *argp) {
    (void)args;
    const int digit = *(const int *)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelLockMutex(mutex, 1, &timeout);
    timeout = 2 * TEST_WAIT_US;
    results[digit - 1] = sceKernelWaitCond(cond, &timeout);
    order = (order << 4) | digit;
    sceKernelUnlockMutex(mutex, 1);
    return 0;
}

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

// Prints a call's result, then the condition's waiting threads and each waiter's wait type
static void print_step(const char *step, int result) {
    SceKernelCondInfo info = { .size = sizeof(info) };
    sceKernelGetCondInfo(cond, &info);
    PRINTF(result, "%s: the call", step);
    PRINTF(info.numWaitThreads, "%s: the waiting threads", step);
    for (int i = 0; i < WAITERS; i++)
        PRINTF(wait_type_of(threads[i]), "%s: waiter %s's wait type", step, names[i]);
}

int main(void) {
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", SCE_KERNEL_ATTR_THREAD_PRIO, mutex, NULL);
    for (int i = 0; i < WAITERS; i++) {
        int digit = i + 1;
        threads[i] = sceKernelCreateThread("waiter", waiter, priorities[i], 0x4000, 0, 0, NULL);
        sceKernelStartThread(threads[i], sizeof(digit), &digit);
        TEST_WAIT_FOR(wait_type_of(threads[i]) == WAIT_TYPE_COND);
    }

    // The main thread holds the mutex during the signals, so each signaled waiter queues on it
    SceUInt32 lock_timeout = TEST_WAIT_US;
    sceKernelLockMutex(mutex, 1, &lock_timeout);
    const int to_a = sceKernelSignalCondTo(cond, threads[0]);
    TEST_WAIT_FOR(wait_type_of(threads[0]) == WAIT_TYPE_COND_MUTEX);
    print_step("signal to A", to_a);

    const int plain = sceKernelSignalCond(cond);
    TEST_WAIT_FOR(wait_type_of(threads[1]) == WAIT_TYPE_COND_MUTEX);
    print_step("plain signal", plain);

    const int all = sceKernelSignalCondAll(cond);
    TEST_WAIT_FOR(wait_type_of(threads[2]) == WAIT_TYPE_COND_MUTEX);
    print_step("signal to all", all);

    const int again = sceKernelSignalCondTo(cond, threads[0]);
    print_step("signal to A again", again);

    sceKernelUnlockMutex(mutex, 1);
    for (int i = 0; i < WAITERS; i++)
        test_join_thread(threads[i]);
    PRINT("the order the waiters get the mutex in", order);
    for (int i = 0; i < WAITERS; i++)
        PRINTF(results[i], "waiter %s's wait result", names[i]);

    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(mutex);
    test_exit();
    return 0;
}
