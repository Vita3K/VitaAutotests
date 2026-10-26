// A wait with callbacks that is notified while its thread sleeps, where the callback's first run
// notifies itself again, so the callback is pending again when the wait goes on. This is
// delay_cb_self_notify for a notify that comes during the sleep instead of before the call. The
// main thread waits until T sleeps in the wait with callbacks, notifies T's callback, and waits
// until the callback ran and T sleeps in that wait again. Then it reads how often the callback ran,
// and ends the wait, unless the wait is a delay, which only ends at its deadline. Both threads are
// pinned to core 0 like callback_notify_departure_more, so T only runs once the main thread blocks.
// Every case has a fresh T, callback and target. The cases:
// - sceKernelDelayThreadCB for 2 seconds
// - sceKernelDelayThreadCB200 for 2 seconds
// - sceKernelWaitSignalCB with a timeout of 2 seconds, ended by a signal
// - sceKernelWaitThreadEndCB with a timeout of 2 seconds, ended by the end of its target
// - sceKernelWaitSignalCB with no timeout, ended by a signal
// - sceKernelWaitThreadEndCB with no timeout, ended by the end of its target
// The cases with no timeout come last, since a wrong wait only ends with a signal or an end.
// It prints how often the callback ran when T waits again, whether T waits again with the callback
// bit, the wait's result, whether it returned before 1 second, the result of sceKernelCheckCallback
// that T calls after the wait, and how often the callback ran after that check.
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define LONG_US 2000000
#define EARLY_US 1000000
#define LONG_TIMEOUT (3 * TEST_WAIT_US)

typedef enum Kind {
    DELAY,
    DELAY_200,
    SIGNAL,
    THREAD_END,
    SIGNAL_NO_TIMEOUT,
    THREAD_END_NO_TIMEOUT
} Kind;

typedef struct Case {
    const char *name;
    Kind kind;
    // The wait type of T's wait without WAIT_TYPE_CB
    SceUInt32 wait_type;
} Case;

static Kind current;
static SceUID callback, gate, target, waiter_thread;
static volatile int callback_runs;
static volatile int result, check, runs_after_check;
static volatile SceUInt64 elapsed;

// Counts its runs and notifies itself on the first one, so it is pending again after that run
static int notified_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    if (callback_runs == 1)
        sceKernelNotifyCallback(callback, 1);
    return 0;
}

// Waits for the gate, with a timeout longer than any case, so it ends when the gate is signaled
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = LONG_TIMEOUT;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// Creates its callback, then waits with callbacks in the kind of the current case, and checks the
// callback once the wait returned
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("notified", 0, notified_callback, NULL);
    SceUInt timeout = LONG_US;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    switch (current) {
    case DELAY: result = sceKernelDelayThreadCB(LONG_US); break;
    case DELAY_200: result = sceKernelDelayThreadCB200(LONG_US); break;
    case SIGNAL: result = sceKernelWaitSignalCB(0, 0, &timeout); break;
    case THREAD_END: result = sceKernelWaitThreadEndCB(target, NULL, &timeout); break;
    case SIGNAL_NO_TIMEOUT: result = sceKernelWaitSignalCB(0, 0, NULL); break;
    case THREAD_END_NO_TIMEOUT: result = sceKernelWaitThreadEndCB(target, NULL, NULL); break;
    }
    elapsed = sceKernelGetProcessTimeWide() - start;
    check = sceKernelCheckCallback();
    runs_after_check = callback_runs;
    sceKernelDeleteCallback(callback);
    return 0;
}

// Returns whether T waits with callbacks in the wait type of the case
static int waits(SceUInt32 wait_type) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(waiter_thread, &info) == 0
        && info.waitType == (wait_type | WAIT_TYPE_CB);
}

// Ends T's wait with the event of its kind. A delay has no event and ends at its deadline.
static void end_wait(Kind kind) {
    switch (kind) {
    case SIGNAL:
    case SIGNAL_NO_TIMEOUT: sceKernelSendSignal(waiter_thread); break;
    case THREAD_END:
    case THREAD_END_NO_TIMEOUT: sceKernelSignalSema(gate, 1); break;
    case DELAY:
    case DELAY_200: break;
    }
}

static void run_case(const Case *c) {
    current = c->kind;
    callback = -1;
    callback_runs = 0;
    result = 0xDEAD;
    check = 0xDEAD;
    runs_after_check = 0xDEAD;
    elapsed = 0;
    if (c->kind == THREAD_END || c->kind == THREAD_END_NO_TIMEOUT) {
        gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
        target = sceKernelCreateThread("target", target_entry, WAITER_PRIORITY, 0x4000, 0,
                                       SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(target, 0, NULL);
    }
    waiter_thread = sceKernelCreateThread("t", waiter, WAITER_PRIORITY, 0x4000, 0,
                                          SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(waiter_thread, 0, NULL);
    TEST_WAIT_FOR(waits(c->wait_type));

    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(callback_runs > 0 && waits(c->wait_type));
    const int runs = callback_runs;
    const int again = waits(c->wait_type);

    end_wait(c->kind);
    test_join_thread(waiter_thread);
    if (c->kind == THREAD_END || c->kind == THREAD_END_NO_TIMEOUT) {
        test_join_thread(target);
        sceKernelDeleteSema(gate);
    }

    PRINTF(runs, "%s: runs of the callback when T waits again", c->name);
    PRINTF(again, "%s: T waits with callbacks again", c->name);
    PRINTF(result, "%s: the result", c->name);
    PRINTF(elapsed < EARLY_US, "%s: it returned before 1 s", c->name);
    PRINTF(check, "%s: the result of sceKernelCheckCallback", c->name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", c->name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    static const Case cases[] = {
        { "delay with callbacks", DELAY, WAIT_TYPE_DELAY },
        { "delay with callbacks, 200 variant", DELAY_200, WAIT_TYPE_DELAY },
        { "signal wait with callbacks", SIGNAL, WAIT_TYPE_SIGNAL },
        { "thread end wait with callbacks", THREAD_END, WAIT_TYPE_THREAD_END },
        { "signal wait with callbacks and no timeout", SIGNAL_NO_TIMEOUT, WAIT_TYPE_SIGNAL },
        { "thread end wait with callbacks and no timeout", THREAD_END_NO_TIMEOUT,
          WAIT_TYPE_THREAD_END },
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        run_case(&cases[i]);

    test_exit();
    return 0;
}
