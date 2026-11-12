// Whether a callback that is notified while a thread waits in sceKernelWaitThreadEndCB runs before
// that wait returns, when the thread the wait is for ends after the notify but before the waiter
// runs again. Every thread is pinned to core 0. The main thread runs at 0x48, the target T at 0x50
// and the waiter W at 0xB0. T waits on a gate semaphore and then returns 0x5A. W creates its
// callback in its own thread and waits for T's end with callbacks, with its status variable preset
// to 0xDEAD. The callback counts its runs and records T's status at its first run. After the wait
// W records its result, the status it got, its time left and how often the callback ran, then calls
// sceKernelCheckCallback and records its result and how often the callback ran after it. Every case
// has a fresh gate, T, W and callback. The cases:
// - notify then end: the main thread notifies W's callback and then signals the gate, with no call
//   that blocks in between. W stays ready below the main thread, and once the main thread blocks T
//   runs first, since it is above W, and ends before W runs again.
// - notify alone: the main thread notifies W's callback and waits until the callback ran and W
//   waits for T's end again, while T still waits on the gate. Then it signals the gate.
// - notify then end, no timeout: the first case with a NULL timeout. It comes last, since a wrong
//   wait has no timeout to end it.
// The timeouts are 6 seconds, far longer than any case takes.
// Each case prints whether W waits for T's end with callbacks before the notify, the notify's
// result, W's status after it, how often the callback ran and whether W waits for T's end with
// callbacks before the gate is signaled (only in the notify alone case), the gate signal's result,
// T's status when the callback first ran, the wait's result, the status it got, whether the time
// left is above 1 s (not in the case without a timeout), the runs of the callback when the wait
// returned, the check's result and the runs after the check.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x48
#define T_PRIORITY 0x50
#define W_PRIORITY 0xB0
#define WAIT_US 6000000
#define EXIT_STATUS 0x5A
#define NOT_DONE 0xDEAD

static SceUID gate, target, callback;
static int use_timeout;
static volatile int callback_ready, callback_runs, done;
static volatile int t_status_in_callback, wait_result, end_status, time_left_above_1s;
static volatile int runs_in_wait, check_result, runs_after_check;

static int status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

// Counts its runs and records T's status at the first one
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    if (callback_runs == 0)
        t_status_in_callback = status_of(target);
    callback_runs++;
    return 0;
}

// Waits for the gate, then ends with EXIT_STATUS
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return EXIT_STATUS;
}

// Waits for T's end with callbacks and records what the wait and the check after it give
static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = WAIT_US;
    SceInt32 status = NOT_DONE;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    callback_ready = 1;
    wait_result = sceKernelWaitThreadEndCB(target, &status, use_timeout ? &timeout : NULL);
    end_status = status;
    time_left_above_1s = timeout > 1000000;
    runs_in_wait = callback_runs;
    check_result = sceKernelCheckCallback();
    runs_after_check = callback_runs;
    sceKernelDeleteCallback(callback);
    done = 1;
    return 0;
}

static SceUID start(const char *name, SceKernelThreadEntry entry, int priority) {
    const SceUID thread = sceKernelCreateThread(name, entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

static int w_waits(SceUID w) {
    return test_in_wait(w, WAIT_TYPE_THREAD_END | WAIT_TYPE_CB, target);
}

static void run_case(const char *name, int end_before_w_runs, int timeout) {
    use_timeout = timeout;
    callback_ready = callback_runs = done = 0;
    t_status_in_callback = wait_result = end_status = time_left_above_1s = NOT_DONE;
    runs_in_wait = check_result = runs_after_check = NOT_DONE;
    int runs_before_end = NOT_DONE, waits_before_end = NOT_DONE;
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    target = start("t", t_entry, T_PRIORITY);
    TEST_WAIT_FOR(test_in_wait(target, WAIT_TYPE_SEMA, gate));
    const SceUID w = start("w", w_entry, W_PRIORITY);
    TEST_WAIT_FOR(callback_ready && w_waits(w));
    const int waiting = w_waits(w);
    const int notify = sceKernelNotifyCallback(callback, 1);
    const int status_notify = status_of(w);
    if (!end_before_w_runs) {
        TEST_WAIT_FOR(callback_runs > 0 && w_waits(w));
        runs_before_end = callback_runs;
        waits_before_end = w_waits(w);
    }
    const int signal = sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(done);
    test_join_thread(w);
    test_join_thread(target);
    sceKernelDeleteSema(gate);

    PRINTF(waiting, "%s: W waits for T's end with callbacks before the notify", name);
    PRINTF(notify, "%s: the notify", name);
    PRINTF(status_notify, "%s: W's status after the notify", name);
    if (!end_before_w_runs) {
        PRINTF(runs_before_end, "%s: runs of the callback before the gate is signaled", name);
        PRINTF(waits_before_end, "%s: W waits for T's end with callbacks before the gate is signaled", name);
    }
    PRINTF(signal, "%s: the gate signal", name);
    PRINTF(t_status_in_callback, "%s: T's status when the callback first ran", name);
    PRINTF(wait_result, "%s: the wait's result", name);
    PRINTF(end_status, "%s: the status the wait got", name);
    if (timeout)
        PRINTF(time_left_above_1s, "%s: the time left is above 1 s", name);
    PRINTF(runs_in_wait, "%s: runs of the callback when the wait returned", name);
    PRINTF(check_result, "%s: the check's result", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("notify then end", 1, 1);
    run_case("notify alone", 0, 1);
    run_case("notify then end, no timeout", 1, 0);

    test_exit();
    return 0;
}
