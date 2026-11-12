// A callback that a thread runs in sceDisplayWaitVblankStartCB lasts past the vblank the wait is
// for. The waiter W creates its callback, waits for a vblank without callbacks and then for the
// next one with callbacks. The first run of the callback polls sceDisplayGetVcount with 1 ms delays
// until 3 vblanks passed since it started. It records the vblanks since W's wait began when it
// starts and when it ends. Later runs only count. After the wait W records its result, the vblanks
// since it began and the runs of the callback, then calls sceKernelCheckCallback and records its
// result and the runs after it. Each case has a fresh W and callback. The cases:
// - notified: the main thread notifies the callback once W is in the wait
// - the display's callback: W registers the callback with sceDisplayRegisterVblankStartCallback
//   before its wait, so the vblank the wait is for notifies it, and every vblank after it too. W
//   unregisters it once its wait returned, before the check.
#include "vblank_cb.h"

static int use_display_callback;
static SceUID callback;
static volatile int ready, done, v0;
static volatile int runs, start_vblanks, end_vblanks;
static volatile int reg, unreg, wait_result, wait_vblanks, runs_in_wait, check_result, runs_after_check;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    runs++;
    if (runs == 1) {
        start_vblanks = vblanks_since(v0);
        const int start = sceDisplayGetVcount();
        TEST_WAIT_FOR(vblanks_since(start) >= 3);
        end_vblanks = vblanks_since(v0);
    }
    return 0;
}

static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    v0 = align_to_vblank();
    if (use_display_callback)
        reg = sceDisplayRegisterVblankStartCallback(callback);
    ready = 1;
    wait_result = sceDisplayWaitVblankStartCB();
    wait_vblanks = vblanks_since(v0);
    runs_in_wait = runs;
    if (use_display_callback)
        unreg = sceDisplayUnregisterVblankStartCallback(callback);
    check_result = sceKernelCheckCallback();
    runs_after_check = runs;
    sceKernelDeleteCallback(callback);
    done = 1;
    return 0;
}

static void run_case(const char *name, int display_callback) {
    use_display_callback = display_callback;
    ready = done = runs = 0;
    start_vblanks = end_vblanks = NOT_DONE;
    reg = unreg = wait_result = wait_vblanks = runs_in_wait = check_result = runs_after_check = NOT_DONE;
    int notify = NOT_DONE;
    const SceUID w = start_w(w_entry);
    TEST_WAIT_FOR(ready && in_cb_wait(w));
    if (!use_display_callback)
        notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(done);
    test_join_thread(w);

    if (use_display_callback)
        PRINTF(reg, "%s: the register", name);
    else
        PRINTF(notify, "%s: the notify", name);
    PRINTF(wait_result, "%s: the wait's result", name);
    PRINTF(runs_in_wait, "%s: runs of the callback when the wait returned", name);
    PRINTF(start_vblanks == 0, "%s: the callback started before the vblank the wait is for", name);
    PRINTF(end_vblanks - start_vblanks >= 3, "%s: the callback lasted at least 3 vblanks", name);
    PRINTF(wait_vblanks - end_vblanks == 0, "%s: the wait returned in the frame the callback ended in", name);
    PRINTF(wait_vblanks - end_vblanks <= 1, "%s: the wait returned at most 1 vblank after the callback ended", name);
    if (use_display_callback)
        PRINTF(unreg, "%s: the unregister", name);
    PRINTF(check_result, "%s: the check's result", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
}

int main(void) {
    pin_main();
    run_case("notified", 0);
    run_case("the display's callback", 1);
    test_exit();
    return 0;
}
