// A wait with callbacks that starts with a callback pending, where the callback's first run
// notifies itself again. The main thread creates the callback and notifies it, which leaves it
// pending since the thread isn't waiting yet. The callback counts its runs and notifies itself on
// its first run, so it is pending again after that run. Then the main thread calls the wait with a
// timeout of 2 seconds, and after it returned it calls sceKernelCheckCallback. Every case has a
// fresh callback.
// It prints the wait's result, whether it returned before 1 second, how often the callback had run
// when the wait returned, the result of the check, and how often the callback had run after that.
// The cases:
// - sceKernelDelayThreadCB for 2000000 microseconds
// - sceKernelWaitSignalCB with a timeout of 2000000 microseconds and no signal sent
// - sceKernelWaitThreadEndCB on a thread that waits on a gate semaphore. The gate is signaled after
//   the case, so the thread ends then, and the wait has its timeout of 2 seconds until then.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define LONG_US 2000000
#define EARLY_US 1000000

typedef enum Kind { DELAY, SIGNAL, THREAD_END } Kind;

static SceUID callback, gate, target;
static volatile int callback_runs;

// Counts its runs and notifies itself on the first one
static int callback_entry(int notify_id, int count, int arg, void *common) {
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
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

static void run_case(const char *name, Kind kind) {
    callback_runs = 0;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    if (kind == THREAD_END) {
        gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
        target = test_start_thread(target_entry);
        test_wait_until_waiting(target);
    }
    sceKernelNotifyCallback(callback, 1);

    SceUInt timeout = LONG_US;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    int result = 0;
    switch (kind) {
    case DELAY:
        result = sceKernelDelayThreadCB(LONG_US);
        break;
    case SIGNAL:
        result = sceKernelWaitSignalCB(0, 0, &timeout);
        break;
    case THREAD_END:
        result = sceKernelWaitThreadEndCB(target, NULL, &timeout);
        break;
    }
    const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - start;
    const int runs_at_return = callback_runs;
    const int check = sceKernelCheckCallback();
    const int runs_after_check = callback_runs;

    sceKernelDeleteCallback(callback);
    if (kind == THREAD_END) {
        sceKernelSignalSema(gate, 1);
        test_join_thread(target);
        sceKernelDeleteSema(gate);
    }

    PRINTF(result, "%s: the result", name);
    PRINTF(elapsed < EARLY_US, "%s: it returned before 1 s", name);
    PRINTF(runs_at_return, "%s: runs of the callback when it returned", name);
    PRINTF(check, "%s: the result of sceKernelCheckCallback", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
}

int main(void) {
    run_case("delay with callbacks", DELAY);
    run_case("signal wait with callbacks", SIGNAL);
    run_case("thread end wait with callbacks", THREAD_END);

    test_exit();
    return 0;
}
