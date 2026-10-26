// Races on a thread's start, and restarts of a thread that returns at once. Three parts. Part one:
// 20 rounds where two threads, on cores 1 and 2, start the same new dormant thread at once. Part
// two: 200 starts of a thread that returns at once, each retried until it gets 0. Part three: a
// thread that exit-deletes itself, started until a start reports the unknown thread error. In part
// one the racing threads are queued on a gate before it opens, and the round's thread waits on a
// semaphore that main signals after the race, so it stays out of DORMANT until then. A start racing
// a delete is left out, since its results differed between two runs on a PS Vita TV. The results
// are printed as comparisons, so the output doesn't depend on which racer wins a round. Every start
// result other than 0, NOT_DORMANT or the unknown thread error is counted.
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define ROUNDS 20
#define RESTARTS 200
// How many times a retry loop calls StartThread, with a millisecond between calls
#define RETRY_TRIES 5000

// The errors as ints, to compare with the int results
#define NOT_DORMANT ((int)SCE_KERNEL_ERROR_NOT_DORMANT)
#define UNKNOWN_THREAD_ID ((int)SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID)

static SceUID gate, hold, target;
static volatile int runs;
// The results of the racing starts of a round, by index
static volatile int results[2];
static int other_errors;

// Counts a result that is neither 0, NOT_DORMANT nor the unknown thread error. Only main calls it.
static void count_other(int result) {
    if (result != 0 && result != NOT_DORMANT && result != UNKNOWN_THREAD_ID)
        other_errors++;
}

// Counts a run, then waits on hold, so the thread isn't DORMANT until main signals it
static int holder_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    runs++;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(hold, 1, &timeout);
    return 0;
}

// Counts a run and returns at once
static int quick_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    runs++;
    return 0;
}

// Exits and deletes itself
static int exit_delete_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelExitDeleteThread(0);
    return 0;
}

// Waits at the gate, then starts the round's thread and keeps the result for its index
static int starter_entry(SceSize args, void *argp) {
    (void)args;
    const int index = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelWaitSema(gate, 1, &timeout) == 0)
        results[index] = sceKernelStartThread(target, 0, NULL);
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

// Calls StartThread on thread until it returns 0 or the tries run out, and returns whether it got 0
static int start_until_zero(SceUID thread) {
    for (int tries = 0; tries < RETRY_TRIES; tries++) {
        const int result = sceKernelStartThread(thread, 0, NULL);
        count_other(result);
        if (result == 0)
            return 1;
        sceKernelDelayThread(1000);
    }
    return 0;
}

// Calls StartThread on an exit-deleted thread until it returns the unknown thread error or the
// tries run out. Sets *ever_zero if any call returns 0, and returns whether the error came.
static int start_until_unknown(SceUID thread, int *ever_zero) {
    for (int tries = 0; tries < RETRY_TRIES; tries++) {
        const int result = sceKernelStartThread(thread, 0, NULL);
        count_other(result);
        if (result == 0)
            *ever_zero = 1;
        if (result == UNKNOWN_THREAD_ID)
            return 1;
        sceKernelDelayThread(1000);
    }
    return 0;
}

// Two starts of one dormant thread, from two threads at once
static void race_two_starts(void) {
    int both_zero = 0, one_each = 0;
    runs = 0;
    for (int round = 0; round < ROUNDS; round++) {
        target = sceKernelCreateThread("target", holder_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
        results[0] = results[1] = 0xDEAD;
        const SceUID starters[2] = {
            start_on(starter_entry, 0, SCE_KERNEL_CPU_MASK_USER_1),
            start_on(starter_entry, 1, SCE_KERNEL_CPU_MASK_USER_2),
        };
        // Both are queued on the gate before it opens
        TEST_WAIT_FOR(test_in_wait(starters[0], WAIT_TYPE_SEMA, gate) && test_in_wait(starters[1], WAIT_TYPE_SEMA, gate));
        sceKernelSignalSema(gate, 2);
        test_join_thread(starters[0]);
        test_join_thread(starters[1]);

        const int a = results[0], b = results[1];
        count_other(a);
        count_other(b);
        if (a == 0 && b == 0)
            both_zero++;
        else if ((a == 0 && b == NOT_DORMANT) || (a == NOT_DORMANT && b == 0))
            one_each++;

        // The round's thread runs once if a start got 0, so let it finish before the next round
        if (a == 0 || b == 0) {
            sceKernelSignalSema(hold, 1);
            test_join_thread(target);
        } else {
            sceKernelDeleteThread(target);
        }
    }
    PRINT("20 rounds of two starts of one dormant thread: no round had both starts return 0", both_zero == 0);
    PRINT("20 rounds of two starts of one dormant thread: every round had one start return 0 and the other NOT_DORMANT", one_each == ROUNDS);
    PRINT("20 rounds of two starts of one dormant thread: the thread ran once per round", runs == ROUNDS);
}

// Starts a thread that returns at once 200 times, retrying each start until it gets 0
static void restart_returning_thread(void) {
    runs = 0;
    target = sceKernelCreateThread("target", quick_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    int restarted = 0;
    for (int round = 0; round < RESTARTS; round++)
        restarted += start_until_zero(target);
    // The last run may still be going, so wait until the thread is dormant again
    TEST_WAIT_FOR(thread_dormant(target));
    PRINT("200 restarts of a thread that returns at once: every restart got 0", restarted == RESTARTS);
    PRINT("200 restarts of a thread that returns at once: the thread ran once per restart", runs == RESTARTS);
    sceKernelDeleteThread(target);
}

// Starts a thread that exit-deletes itself until a start reports the unknown thread error
static void start_exit_deleted_thread(void) {
    int ever_zero = 0;
    target = sceKernelCreateThread("exiting", exit_delete_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    const int first = sceKernelStartThread(target, 0, NULL);
    count_other(first);
    PRINT("the first start of the thread that exit-deletes itself returned 0", first == 0);
    const int reached = start_until_unknown(target, &ever_zero);
    PRINT("starts of the exit-deleted thread: the unknown thread error was reached", reached);
    PRINT("starts of the exit-deleted thread: none returned 0 after the first", ever_zero == 0);
}

int main(void) {
    gate = sceKernelCreateSema("gate", 0, 0, 2, NULL);
    hold = sceKernelCreateSema("hold", 0, 0, 1, NULL);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    race_two_starts();
    restart_returning_thread();
    start_exit_deleted_thread();

    PRINT("no start returned an error other than 0x80028028 or 0x80028021", other_errors == 0);
    sceKernelDeleteSema(gate);
    sceKernelDeleteSema(hold);
    test_exit();
    return 0;
}
