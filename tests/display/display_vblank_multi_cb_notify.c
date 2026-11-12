// A callback notified while a thread waits in sceDisplayWaitVblankStartMultiCB for 4 vblanks.
// Whether the wait still ends 4 vblanks after it began. The waiter W creates its callback, waits
// for a vblank without callbacks and then for 4 with callbacks. The main thread waits until the
// given number of vblanks passed since W's wait began and notifies the callback. The callback
// records the vblanks since W's wait began when it starts and when it ends. After the wait W
// records its result, the vblanks since it began and the runs of the callback, then calls
// sceKernelCheckCallback and records its result and the runs after it. Each case has a fresh W
// and callback. The cases:
// - notify after 2 vblanks: the callback returns at once
// - a callback past the target: the notify comes after 1 vblank, and the callback polls
//   sceDisplayGetVcount with 1 ms delays until 5 vblanks passed since W's wait began, one past the
//   vblank the wait is for
#include "vblank_cb.h"

#define VBLANKS 4

static int hold_until;
static SceUID callback;
static volatile int ready, done, v0;
static volatile int runs, start_vblanks, end_vblanks;
static volatile int wait_result, wait_vblanks, runs_in_wait, check_result, runs_after_check;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    runs++;
    start_vblanks = vblanks_since(v0);
    if (hold_until)
        TEST_WAIT_FOR(vblanks_since(v0) >= hold_until);
    end_vblanks = vblanks_since(v0);
    return 0;
}

static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    v0 = align_to_vblank();
    ready = 1;
    wait_result = sceDisplayWaitVblankStartMultiCB(VBLANKS);
    wait_vblanks = vblanks_since(v0);
    runs_in_wait = runs;
    check_result = sceKernelCheckCallback();
    runs_after_check = runs;
    sceKernelDeleteCallback(callback);
    done = 1;
    return 0;
}

static void run_case(const char *name, int notify_at, int hold) {
    hold_until = hold;
    ready = done = runs = 0;
    start_vblanks = end_vblanks = NOT_DONE;
    wait_result = wait_vblanks = runs_in_wait = check_result = runs_after_check = NOT_DONE;
    const SceUID w = start_w(w_entry);
    TEST_WAIT_FOR(ready && in_cb_wait(w));
    const int wait_type = wait_type_of(w);
    TEST_WAIT_FOR(vblanks_since(v0) >= notify_at);
    const int notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(done);
    test_join_thread(w);

    PRINTF(wait_type, "%s: W's wait type in the wait", name);
    PRINTF(notify, "%s: the notify", name);
    PRINTF(wait_result, "%s: the wait's result", name);
    PRINTF(runs_in_wait, "%s: runs of the callback when the wait returned", name);
    PRINTF(start_vblanks == notify_at, "%s: the callback started %d vblank%s after the wait began", name, notify_at, notify_at == 1 ? "" : "s");
    PRINTF(wait_vblanks >= VBLANKS, "%s: the wait returned at least %d vblanks after it began", name, VBLANKS);
    PRINTF(wait_vblanks <= VBLANKS + 1, "%s: the wait returned at most %d vblanks after it began", name, VBLANKS + 1);
    PRINTF(wait_vblanks - end_vblanks == 0, "%s: the wait returned in the frame the callback ended in", name);
    PRINTF(wait_vblanks - end_vblanks <= 1, "%s: the wait returned at most 1 vblank after the callback ended", name);
    PRINTF(check_result, "%s: the check's result", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
}

int main(void) {
    pin_main();
    run_case("notify after 2 vblanks", 2, 0);
    run_case("a callback past the target", 1, VBLANKS + 1);
    test_exit();
    return 0;
}
