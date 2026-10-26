// Two deletes of one thread that ends, raced by two threads, while an end wait for it runs too.
// Each of 20 rounds starts a thread that returns at once and waits for its end, so the thread is
// dormant before the race starts. Then two deleter threads, on cores 1 and 2, and an end waiter
// on core 0 wait on a gate. The main thread, on core 0, signals the gate with a count of 3, so all
// three go at once. Each deleter calls sceKernelDeleteThread on the thread, and the waiter calls
// sceKernelWaitThreadEnd on it with a 1 second timeout. The test prints how many rounds had both
// deletes return 0, how many had one return 0 and the other the unknown thread error or
// NOT_DORMANT, how many had any other pair of results, and how many end waits timed out. The
// losing delete gets either error, depending on whether the winner had finished, so the two are
// counted together, and the counts don't depend on which deleter wins.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define ROUNDS 20
// The end waiter's timeout, 1 second
#define END_WAIT_US 1000000
// The errors as ints, to compare with the int results
#define UNKNOWN_THREAD_ID ((int)SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID)
#define WAIT_TIMEOUT ((int)SCE_KERNEL_ERROR_WAIT_TIMEOUT)
#define NOT_DORMANT ((int)SCE_KERNEL_ERROR_NOT_DORMANT)

static SceUID gate, target;
static volatile int delete_result[2], waiter_result;

static int returns_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Starts entry on a new thread pinned to mask, passing it index as its argument
static SceUID start_on(SceKernelThreadEntry entry, int index, SceUInt32 mask) {
    const SceUID thread = sceKernelCreateThread("race", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, mask, NULL);
    sceKernelStartThread(thread, sizeof(index), &index);
    return thread;
}

// Returns whether thread is DORMANT, which it is once its exit has finished
static int thread_dormant(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.status == SCE_THREAD_DORMANT;
}

// Waits at the gate, then deletes the round's thread and keeps the result for its index
static int deleter_entry(SceSize args, void *argp) {
    (void)args;
    const int index = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelWaitSema(gate, 1, &timeout) == 0)
        delete_result[index] = sceKernelDeleteThread(target);
    return 0;
}

// Waits at the gate, then waits for the end of the round's thread with a 1 second timeout
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelWaitSema(gate, 1, &timeout) == 0) {
        timeout = END_WAIT_US;
        waiter_result = sceKernelWaitThreadEnd(target, NULL, &timeout);
    }
    return 0;
}

int main(void) {
    gate = sceKernelCreateSema("gate", 0, 0, 3, NULL);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    int both_zero = 0, one_zero = 0, other = 0, timed_out = 0;
    for (int round = 0; round < ROUNDS; round++) {
        target = sceKernelCreateThread("target", returns_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
        sceKernelStartThread(target, 0, NULL);
        // The thread's end wait can return before its state reads DORMANT, so the race starts only
        // once it does, and the deletes can't fail for the thread still running
        sceKernelWaitThreadEnd(target, NULL, NULL);
        TEST_WAIT_FOR(thread_dormant(target));

        delete_result[0] = delete_result[1] = 0xDEAD;
        waiter_result = 0xDEAD;
        const SceUID deleters[2] = {
            start_on(deleter_entry, 0, SCE_KERNEL_CPU_MASK_USER_1),
            start_on(deleter_entry, 1, SCE_KERNEL_CPU_MASK_USER_2),
        };
        const SceUID waiter = sceKernelCreateThread("waiter", waiter_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(waiter, 0, NULL);
        // All three are queued on the gate before it opens
        TEST_WAIT_FOR(test_in_wait(deleters[0], WAIT_TYPE_SEMA, gate) && test_in_wait(deleters[1], WAIT_TYPE_SEMA, gate) && test_in_wait(waiter, WAIT_TYPE_SEMA, gate));
        sceKernelSignalSema(gate, 3);
        test_join_thread(deleters[0]);
        test_join_thread(deleters[1]);
        test_join_thread(waiter);

        const int a = delete_result[0], b = delete_result[1];
        if (a == 0 && b == 0)
            both_zero++;
        else if ((a == 0 && (b == UNKNOWN_THREAD_ID || b == NOT_DORMANT)) || ((a == UNKNOWN_THREAD_ID || a == NOT_DORMANT) && b == 0))
            one_zero++;
        else
            other++;
        timed_out += waiter_result == WAIT_TIMEOUT;
    }
    PRINT("20 rounds of two deletes: how many had both return 0", both_zero);
    PRINT("20 rounds of two deletes: how many had one return 0 and the other the unknown thread error or NOT_DORMANT", one_zero);
    PRINT("20 rounds of two deletes: how many had any other pair of results", other);
    PRINT("20 rounds of two deletes: how many end waits timed out", timed_out);

    sceKernelDeleteSema(gate);
    test_exit();
    return 0;
}
