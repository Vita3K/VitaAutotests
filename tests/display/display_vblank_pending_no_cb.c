// A callback notified while its thread waits in sceDisplayWaitVblankStart, which runs no callbacks.
// Whether it stays pending until a later wait with callbacks or a sceKernelCheckCallback. The
// callback counts its runs and records the count it got at its first run. Each case has a fresh
// callback. The cases:
// - a later wait with callbacks: the waiter W creates its callback, waits for a vblank, and then
//   for the next one, both without callbacks. The main thread notifies the callback during the
//   second wait, and reads W's status and the callback's notify count after the notify. W records
//   the runs after that wait and after another one without callbacks, then waits for a vblank
//   with sceDisplayWaitVblankStartCB. The callback records the vblanks since that wait began at its
//   first run. W records the runs after that wait, then calls sceKernelCheckCallback and records
//   its result and the runs after it.
// - a check: like the first case, but after its wait without callbacks W calls
//   sceKernelCheckCallback right away and records its result and the runs after it
// - the display's callback: the main thread creates the callback, waits for a vblank, registers it
//   with sceDisplayRegisterVblankStartCallback and waits for 3 vblanks with
//   sceDisplayWaitVblankStartMulti. Then it records the callback's notify count and runs,
//   unregisters it, calls sceKernelCheckCallback and records its result and the runs after it.
#include "vblank_cb.h"

enum Mode { LATER_CB_WAIT, CHECK, DISPLAY_CALLBACK };

static int mode;
static SceUID callback;
static volatile int ready, done, v0;
static volatile int runs, first_count, first_run_vblanks;
static volatile int wait_result, runs_after_wait, second_result, runs_after_second, cb_wait_result, runs_after_cb_wait;
static volatile int check_result, runs_after_check;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)arg;
    (void)common;
    if (runs == 0) {
        first_count = count;
        first_run_vblanks = vblanks_since(v0);
    }
    runs++;
    return 0;
}

static int notify_count(SceUID id) {
    CallbackInfo info = { .size = sizeof(info) };
    const int result = sceKernelGetCallbackInfo(id, (SceKernelCallbackInfo *)&info);
    return result < 0 ? result : info.notifyCount;
}

static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    v0 = sceDisplayGetVcount();
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    align_to_vblank();
    ready = 1;
    wait_result = sceDisplayWaitVblankStart();
    runs_after_wait = runs;
    if (mode == LATER_CB_WAIT) {
        second_result = sceDisplayWaitVblankStart();
        runs_after_second = runs;
        v0 = sceDisplayGetVcount();
        cb_wait_result = sceDisplayWaitVblankStartCB();
        runs_after_cb_wait = runs;
    }
    check_result = sceKernelCheckCallback();
    runs_after_check = runs;
    sceKernelDeleteCallback(callback);
    done = 1;
    return 0;
}

static void reset(int case_mode) {
    mode = case_mode;
    ready = done = runs = 0;
    first_count = first_run_vblanks = NOT_DONE;
    wait_result = runs_after_wait = second_result = runs_after_second = cb_wait_result = runs_after_cb_wait = NOT_DONE;
    check_result = runs_after_check = NOT_DONE;
}

static void run_w_case(const char *name, int case_mode) {
    reset(case_mode);
    const SceUID w = start_w(w_entry);
    TEST_WAIT_FOR(ready && wait_type_of(w) != 0);
    const int wait_type = wait_type_of(w);
    const int notify = sceKernelNotifyCallback(callback, 1);
    const int status = status_of(w);
    const int count = notify_count(callback);
    TEST_WAIT_FOR(done);
    test_join_thread(w);

    PRINTF(wait_type, "%s: W's wait type in the wait", name);
    PRINTF(notify, "%s: the notify", name);
    PRINTF(status, "%s: W's status after the notify", name);
    PRINTF(count, "%s: the callback's notify count after the notify", name);
    PRINTF(wait_result, "%s: the wait's result", name);
    PRINTF(runs_after_wait, "%s: runs of the callback after the wait", name);
    if (mode == LATER_CB_WAIT) {
        PRINTF(second_result, "%s: the second wait's result", name);
        PRINTF(runs_after_second, "%s: runs of the callback after the second wait", name);
        PRINTF(cb_wait_result, "%s: the wait with callbacks", name);
        PRINTF(runs_after_cb_wait, "%s: runs of the callback after the wait with callbacks", name);
        PRINTF(first_run_vblanks == 0, "%s: the callback ran before the vblank the wait with callbacks is for", name);
    }
    PRINTF(check_result, "%s: the check's result", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
    PRINTF(first_count, "%s: the count the callback got at its first run", name);
}

static void run_display_callback_case(const char *name) {
    reset(DISPLAY_CALLBACK);
    v0 = sceDisplayGetVcount();
    callback = sceKernelCreateCallback("vblank", 0, callback_entry, NULL);
    align_to_vblank();
    const int reg = sceDisplayRegisterVblankStartCallback(callback);
    const int result = sceDisplayWaitVblankStartMulti(3);
    const int count = notify_count(callback);
    const int runs_in_wait = runs;
    const int unreg = sceDisplayUnregisterVblankStartCallback(callback);
    const int check = sceKernelCheckCallback();
    const int runs_after = runs;
    sceKernelDeleteCallback(callback);

    PRINTF(reg, "%s: the register", name);
    PRINTF(result, "%s: the wait's result", name);
    PRINTF(count, "%s: the callback's notify count after the wait", name);
    PRINTF(runs_in_wait, "%s: runs of the callback after the wait", name);
    PRINTF(unreg, "%s: the unregister", name);
    PRINTF(check, "%s: the check's result", name);
    PRINTF(runs_after, "%s: runs of the callback after the check", name);
    PRINTF(first_count, "%s: the count the callback got at its first run", name);
}

int main(void) {
    pin_main();
    run_w_case("a later wait with callbacks", LATER_CB_WAIT);
    run_w_case("a check", CHECK);
    run_display_callback_case("the display's callback");
    test_exit();
    return 0;
}
