// A callback that a thread runs in sceDisplayWaitVblankStartCB waits for a vblank itself with
// sceDisplayWaitVblankStartCB, a nested wait with callbacks. The waiter W creates its callback,
// waits for a vblank without callbacks and then for the next one with callbacks. The main thread
// notifies the callback once W is in that wait, and reads W's wait type once the callback is in its
// nested wait. A run of the callback inside another run only records itself. The outer run records
// the result of its nested wait and the vblanks since W's wait began when it returns. W records its
// steps: 1 when an outer run starts, 4 when a run inside another starts, 2 when the nested wait
// returned, 3 when W's own wait returned. After the wait W records its result, the vblanks since it
// began, the runs of the callback and the deepest nesting, then calls sceKernelCheckCallback and
// records its result and the runs after it. Each case has a fresh W and callback. The cases:
// - nested wait: one notify
// - notify in the nested wait: the main thread notifies the callback again once W is in the
//   nested wait
#include "vblank_cb.h"

static int notify_again;
static SceUID callback;
static volatile int ready, done, v0, in_callback, depth, max_depth;
static volatile int runs, inner_result, inner_vblanks;
static volatile int wait_result, wait_vblanks, runs_in_wait, check_result, runs_after_check;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    runs++;
    depth++;
    if (depth > max_depth)
        max_depth = depth;
    if (depth == 1) {
        step(1);
        in_callback = 1;
        inner_result = sceDisplayWaitVblankStartCB();
        inner_vblanks = vblanks_since(v0);
        step(2);
    } else {
        step(4);
    }
    depth--;
    return 0;
}

static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    v0 = align_to_vblank();
    ready = 1;
    wait_result = sceDisplayWaitVblankStartCB();
    wait_vblanks = vblanks_since(v0);
    step(3);
    runs_in_wait = runs;
    check_result = sceKernelCheckCallback();
    runs_after_check = runs;
    sceKernelDeleteCallback(callback);
    done = 1;
    return 0;
}

static void run_case(const char *name, int again) {
    notify_again = again;
    reset_steps();
    ready = done = in_callback = depth = max_depth = runs = 0;
    inner_result = inner_vblanks = wait_result = wait_vblanks = runs_in_wait = check_result = runs_after_check = NOT_DONE;
    int second_notify = NOT_DONE;
    const SceUID w = start_w(w_entry);
    TEST_WAIT_FOR(ready && in_cb_wait(w));
    const int wait_type = wait_type_of(w);
    const int notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(in_callback && status_of(w) == THREAD_STATUS_WAITING);
    const int inner_wait_type = wait_type_of(w);
    if (notify_again)
        second_notify = sceKernelNotifyCallback(callback, 2);
    TEST_WAIT_FOR(done);
    test_join_thread(w);

    PRINTF(wait_type, "%s: W's wait type in its wait", name);
    PRINTF(notify, "%s: the notify", name);
    PRINTF(inner_wait_type, "%s: W's wait type in the nested wait", name);
    if (notify_again)
        PRINTF(second_notify, "%s: the notify in the nested wait", name);
    PRINTF(inner_result, "%s: the nested wait", name);
    PRINTF(inner_vblanks == 1, "%s: the nested wait returned at the first vblank after W's wait began", name);
    PRINTF(wait_result, "%s: W's wait's result", name);
    PRINTF(runs_in_wait, "%s: runs of the callback when W's wait returned", name);
    PRINTF(max_depth, "%s: the deepest nesting of the callback", name);
    print_steps(name);
    PRINTF(wait_vblanks - inner_vblanks == 0, "%s: W's wait returned in the frame the nested wait returned in", name);
    PRINTF(wait_vblanks - inner_vblanks <= 1, "%s: W's wait returned at most 1 vblank after the nested wait", name);
    PRINTF(check_result, "%s: the check's result", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
}

int main(void) {
    pin_main();
    run_case("nested wait", 0);
    run_case("notify in the nested wait", 1);
    test_exit();
    return 0;
}
