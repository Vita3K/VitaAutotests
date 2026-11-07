// A notify of a callback that is already pending while its thread sleeps in sceKernelWaitSignalCB.
// T's callback notifies itself on its first run, so after that run it is pending again while T
// sleeps in the signal wait. The main thread then notifies the callback a second time, which adds
// to the pending notification, and checks whether that ends T's sleep so the callback runs again,
// and with which count. Then it sends T the signal. Both threads are pinned to core 0 like
// cb_wait_self_notify_during_sleep, so T only runs once the main thread blocks. The cases are a
// signal wait with a timeout of 20 seconds and one with no timeout.
//
// It prints how often the callback ran when T sleeps again after the first notify, whether T then
// waits with the callback bit, how often the callback ran after the second notify, the count its
// second run got, whether T waits with the callback bit again after that, the wait's result, the
// result of sceKernelCheckCallback that T calls after the wait, and how often the callback ran
// after that check.
//
// On a PS Vita TV on firmware 3.65 T sleeps again after the first run with the callback pending,
// and the second notify ends that sleep: the callback runs a second time with the count 2, T waits
// with callbacks again, the signal ends the wait with 0 and the check finds nothing pending.
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
// Longer than both polls of a case, so the wait with a timeout can only end by the signal
#define LONG_US 20000000

static int with_timeout;
static SceUID callback, waiter_thread;
static volatile int callback_runs, second_count;
static volatile int result, check, runs_after_check;

// Counts its runs, keeps the count its second run gets, and notifies itself on the first run
static int notified_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)arg;
    (void)common;
    callback_runs++;
    if (callback_runs == 1)
        sceKernelNotifyCallback(callback, 1);
    else if (callback_runs == 2)
        second_count = count;
    return 0;
}

// Creates its callback, waits for the signal with callbacks, then checks the callback
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("notified", 0, notified_callback, NULL);
    SceUInt timeout = LONG_US;
    result = sceKernelWaitSignalCB(0, 0, with_timeout ? &timeout : NULL);
    check = sceKernelCheckCallback();
    runs_after_check = callback_runs;
    sceKernelDeleteCallback(callback);
    return 0;
}

// Returns whether T waits for the signal with callbacks
static int waits(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(waiter_thread, &info) == 0
        && info.waitType == (WAIT_TYPE_SIGNAL | WAIT_TYPE_CB);
}

static void run_case(const char *name, int timeout) {
    with_timeout = timeout;
    callback = -1;
    callback_runs = 0;
    second_count = 0;
    result = 0xDEAD;
    check = 0xDEAD;
    runs_after_check = 0xDEAD;
    waiter_thread = sceKernelCreateThread("t", waiter, WAITER_PRIORITY, 0x4000, 0,
                                          SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(waiter_thread, 0, NULL);
    TEST_WAIT_FOR(waits());

    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(callback_runs > 0 && waits());
    const int runs_first = callback_runs;
    const int waits_first = waits();

    sceKernelNotifyCallback(callback, 2);
    TEST_WAIT_FOR(callback_runs > 1 && waits());
    const int runs_second = callback_runs;
    const int waits_second = waits();

    sceKernelSendSignal(waiter_thread);
    test_join_thread(waiter_thread);

    PRINTF(runs_first, "%s: runs of the callback when T sleeps again", name);
    PRINTF(waits_first, "%s: T waits with callbacks again", name);
    PRINTF(runs_second, "%s: runs of the callback after the second notify", name);
    PRINTF(second_count, "%s: the count of the second run", name);
    PRINTF(waits_second, "%s: T waits with callbacks after the second notify", name);
    PRINTF(result, "%s: the result", name);
    PRINTF(check, "%s: the result of sceKernelCheckCallback", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("signal wait with callbacks", 1);
    run_case("signal wait with callbacks and no timeout", 0);

    test_exit();
    return 0;
}
