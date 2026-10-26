// Semaphore signals and cancels at the top of the count range, where a count plus a signal can wrap
// past the largest int. Each call is printed with its result and the count after it, and a
// waiter's state right after the call that could wake it. The cases:
// - a semaphore with a maximum and an initial count of 0x7FFFFFFF: a signal of 1 and a signal of
//   0x7FFFFFFF, which both wrap, a poll of 0x7FFFFFFF, a poll of 1, a signal of 0x7FFFFFFF, which
//   takes the count exactly to the maximum, and a signal of 1, which wraps again
// - a semaphore with a maximum of 0x7FFFFFFF and a count of 1: a signal of 0x7FFFFFFF, which wraps,
//   then a poll of 1 and another poll of 1
// - the same semaphore with a waiter for 0x7FFFFFFF queued: the wrapping signal, which must leave
//   the waiter waiting, then a signal of 0x7FFFFFFE, which brings the count to the maximum and
//   serves the waiter
// - a semaphore with a maximum of 0x7FFFFFFF and a count of 5: a cancel to 0x7FFFFFFF, then a
//   cancel to 0x80000000, which sets the count back to its initial count
// The waiter runs on core 0 below the main thread, so it can't run between a call and the reads
// after it. Its wait ends after 6 seconds at the latest.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define MAX_COUNT 0x7FFFFFFF
#define NOT_DONE 0xDEAD

static SceUID sema;
static volatile int waiter_result = NOT_DONE;

// Waits for MAX_COUNT on sema and stores the result
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 3 * TEST_WAIT_US;
    waiter_result = sceKernelWaitSema(sema, MAX_COUNT, &timeout);
    return 0;
}

// Returns the current count of sema
static int count(void) {
    SceKernelSemaInfo info = { .size = sizeof(info) };
    sceKernelGetSemaInfo(sema, &info);
    return info.currentCount;
}

// Starts the waiter, and returns once it waits on sema
static SceUID start_waiter(void) {
    waiter_result = NOT_DONE;
    const SceUID thread = sceKernelCreateThread(
        "waiter", waiter, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_SEMA, sema));
    return thread;
}

static void max_and_init_case(void) {
    const char *name = "max 0x7FFFFFFF and count 0x7FFFFFFF";
    sema = sceKernelCreateSema("sema", 0, MAX_COUNT, MAX_COUNT, NULL);
    PRINTF(sceKernelSignalSema(sema, 1), "%s: signal 1", name);
    PRINTF(count(), "%s: the count after it", name);
    PRINTF(sceKernelSignalSema(sema, MAX_COUNT), "%s: signal 0x7FFFFFFF", name);
    PRINTF(count(), "%s: the count after it", name);
    PRINTF(sceKernelPollSema(sema, MAX_COUNT), "%s: poll 0x7FFFFFFF", name);
    PRINTF(count(), "%s: the count after it", name);
    PRINTF(sceKernelPollSema(sema, 1), "%s: poll 1", name);
    PRINTF(count(), "%s: the count after it", name);
    PRINTF(sceKernelSignalSema(sema, MAX_COUNT), "%s: signal 0x7FFFFFFF to the maximum", name);
    PRINTF(count(), "%s: the count after it", name);
    PRINTF(sceKernelSignalSema(sema, 1), "%s: signal 1 past it", name);
    PRINTF(count(), "%s: the count after it", name);
    sceKernelDeleteSema(sema);
}

static void wrap_case(void) {
    const char *name = "max 0x7FFFFFFF and count 1";
    sema = sceKernelCreateSema("sema", 0, 1, MAX_COUNT, NULL);
    PRINTF(sceKernelSignalSema(sema, MAX_COUNT), "%s: signal 0x7FFFFFFF", name);
    PRINTF(count(), "%s: the count after it", name);
    PRINTF(sceKernelPollSema(sema, 1), "%s: poll 1", name);
    PRINTF(count(), "%s: the count after it", name);
    PRINTF(sceKernelPollSema(sema, 1), "%s: poll 1 again", name);
    PRINTF(count(), "%s: the count after it", name);
    sceKernelDeleteSema(sema);
}

static void waiter_case(void) {
    const char *name = "max 0x7FFFFFFF, count 1, a waiter for 0x7FFFFFFF";
    sema = sceKernelCreateSema("sema", 0, 1, MAX_COUNT, NULL);
    const SceUID thread = start_waiter();
    PRINTF(sceKernelSignalSema(sema, MAX_COUNT), "%s: signal 0x7FFFFFFF", name);
    PRINTF(count(), "%s: the count after it", name);
    PRINTF(test_in_wait(thread, WAIT_TYPE_SEMA, sema), "%s: the waiter still waits", name);
    PRINTF(sceKernelSignalSema(sema, MAX_COUNT - 1), "%s: signal 0x7FFFFFFE", name);
    test_join_thread(thread);
    PRINTF(waiter_result, "%s: the waiter's result", name);
    PRINTF(count(), "%s: the count after the waiter", name);
    sceKernelDeleteSema(sema);
}

static void cancel_case(void) {
    const char *name = "max 0x7FFFFFFF and count 5";
    sema = sceKernelCreateSema("sema", 0, 5, MAX_COUNT, NULL);
    int waiting = NOT_DONE;
    PRINTF(sceKernelCancelSema(sema, MAX_COUNT, &waiting), "%s: cancel to 0x7FFFFFFF", name);
    PRINTF(count(), "%s: the count after it", name);
    PRINTF(waiting, "%s: the waiters it counted", name);
    waiting = NOT_DONE;
    PRINTF(sceKernelCancelSema(sema, (int)0x80000000, &waiting), "%s: cancel to 0x80000000", name);
    PRINTF(count(), "%s: the count after it", name);
    PRINTF(waiting, "%s: the waiters it counted", name);
    sceKernelDeleteSema(sema);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    max_and_init_case();
    wrap_case();
    waiter_case();
    cancel_case();

    test_exit();
    return 0;
}
