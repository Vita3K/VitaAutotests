// A callback notified while a thread waits in sceDisplayWaitVblankStartCB. Whether it runs before
// the wait returns, how often, and when the wait returns. The waiter W creates its callback, waits
// for a vblank without callbacks and then for the next one with callbacks. The callback counts its
// runs, and at its first run records the count and argument it got and the vblanks since W's wait
// began. After the wait W records its result, the vblanks since it began and the runs of the
// callback, then calls sceKernelCheckCallback and records its result and the runs after it. Each
// case has a fresh W and callback. The cases:
// - one notify: the main thread notifies the callback once W is in the wait
// - two notifies: the main thread notifies it twice in a row, before W runs again
// - notify before the wait: the main thread notifies the callback while W waits on a gate semaphore
//   without callbacks and then signals the gate. W then waits for a vblank without callbacks, and
//   then for the next one with callbacks.
// The notifies come a few milliseconds after the vblank W's wait starts after, far from the next.
#include "vblank_cb.h"

enum Mode { ONE_NOTIFY, TWO_NOTIFIES, BEFORE_WAIT };

static int mode;
static SceUID callback, gate;
static volatile int ready, done, v0;
static volatile int runs, first_count, first_arg, first_run_vblanks;
static volatile int runs_before_wait, wait_result, wait_vblanks, runs_in_wait, check_result, runs_after_check;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)common;
    if (runs == 0) {
        first_count = count;
        first_arg = arg;
        first_run_vblanks = vblanks_since(v0);
    }
    runs++;
    return 0;
}

static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    v0 = sceDisplayGetVcount();
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    if (mode == BEFORE_WAIT) {
        ready = 1;
        SceUInt timeout = TEST_WAIT_US;
        sceKernelWaitSema(gate, 1, &timeout);
    }
    v0 = align_to_vblank();
    runs_before_wait = runs;
    ready = 1;
    wait_result = sceDisplayWaitVblankStartCB();
    wait_vblanks = vblanks_since(v0);
    runs_in_wait = runs;
    check_result = sceKernelCheckCallback();
    runs_after_check = runs;
    sceKernelDeleteCallback(callback);
    done = 1;
    return 0;
}

static void run_case(const char *name, int case_mode) {
    mode = case_mode;
    ready = done = runs = 0;
    first_count = first_arg = first_run_vblanks = NOT_DONE;
    runs_before_wait = wait_result = wait_vblanks = runs_in_wait = check_result = runs_after_check = NOT_DONE;
    int wait_type = NOT_DONE, notify = NOT_DONE, second_notify = NOT_DONE, status = NOT_DONE, signal = NOT_DONE;
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    const SceUID w = start_w(w_entry);
    if (mode == BEFORE_WAIT) {
        TEST_WAIT_FOR(ready && test_in_wait(w, WAIT_TYPE_SEMA, gate));
        notify = sceKernelNotifyCallback(callback, 1);
        status = status_of(w);
        signal = sceKernelSignalSema(gate, 1);
    } else {
        TEST_WAIT_FOR(ready && in_cb_wait(w));
        wait_type = wait_type_of(w);
        notify = sceKernelNotifyCallback(callback, 1);
        if (mode == TWO_NOTIFIES)
            second_notify = sceKernelNotifyCallback(callback, 2);
        status = status_of(w);
    }
    TEST_WAIT_FOR(done);
    test_join_thread(w);
    sceKernelDeleteSema(gate);

    if (mode != BEFORE_WAIT)
        PRINTF(wait_type, "%s: W's wait type in the wait", name);
    PRINTF(notify, "%s: the notify", name);
    if (mode == TWO_NOTIFIES)
        PRINTF(second_notify, "%s: the second notify", name);
    PRINTF(status, "%s: W's status after the notify", name);
    if (mode == BEFORE_WAIT) {
        PRINTF(signal, "%s: the gate signal", name);
        PRINTF(runs_before_wait, "%s: runs of the callback before the wait", name);
    }
    PRINTF(wait_result, "%s: the wait's result", name);
    PRINTF(runs_in_wait, "%s: runs of the callback when the wait returned", name);
    PRINTF(first_count, "%s: the count the callback got at its first run", name);
    PRINTF(first_arg, "%s: the argument the callback got at its first run", name);
    PRINTF(first_run_vblanks == 0, "%s: the callback first ran before the next vblank", name);
    PRINTF(wait_vblanks == 1, "%s: the wait returned at the first vblank after it began", name);
    PRINTF(check_result, "%s: the check's result", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
}

int main(void) {
    pin_main();
    run_case("one notify", ONE_NOTIFY);
    run_case("two notifies", TWO_NOTIFIES);
    run_case("notify before the wait", BEFORE_WAIT);
    test_exit();
    return 0;
}
