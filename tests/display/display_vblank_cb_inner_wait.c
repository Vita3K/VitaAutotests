// A callback that a thread runs in sceDisplayWaitVblankStartCB waits for a vblank itself, with
// sceDisplayWaitVblankStart, which runs no callbacks. The waiter W creates its callback, waits for
// a vblank without callbacks and then for the next one with callbacks. The main thread notifies the
// callback once W is in that wait, and reads W's wait type once the callback is in its own wait.
// The callback records the result of its wait and the vblanks since W's wait began when it
// returns. W records its steps: 1 when the callback starts, 2 when its wait returned, 3 when W's
// own wait returned. After the wait W records its result, the vblanks since it began and the runs
// of the callback, then calls sceKernelCheckCallback and records its result. Both waits are for
// the same vblank, the first after W's wait began.
#include "vblank_cb.h"

static SceUID callback;
static volatile int ready, done, v0, in_callback;
static volatile int runs, inner_result, inner_vblanks;
static volatile int wait_result, wait_vblanks, runs_in_wait, check_result;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    step(1);
    in_callback = 1;
    inner_result = sceDisplayWaitVblankStart();
    inner_vblanks = vblanks_since(v0);
    step(2);
    runs++;
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
    sceKernelDeleteCallback(callback);
    done = 1;
    return 0;
}

int main(void) {
    pin_main();
    reset_steps();
    inner_result = inner_vblanks = wait_result = wait_vblanks = runs_in_wait = check_result = NOT_DONE;
    const SceUID w = start_w(w_entry);
    TEST_WAIT_FOR(ready && in_cb_wait(w));
    const int wait_type = wait_type_of(w);
    const int notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(in_callback && status_of(w) == THREAD_STATUS_WAITING);
    const int inner_wait_type = wait_type_of(w);
    TEST_WAIT_FOR(done);
    test_join_thread(w);

    PRINT("W's wait type in its wait", wait_type);
    PRINT("the notify", notify);
    PRINT("W's wait type in the callback's wait", inner_wait_type);
    PRINT("the callback's wait", inner_result);
    PRINT("the callback's wait returned at the first vblank after W's wait began", inner_vblanks == 1);
    PRINT("W's wait's result", wait_result);
    PRINT("runs of the callback when W's wait returned", runs_in_wait);
    print_steps("order");
    PRINT("W's wait returned in the frame the callback ended in", wait_vblanks - inner_vblanks == 0);
    PRINT("W's wait returned at most 1 vblank after the callback ended", wait_vblanks - inner_vblanks <= 1);
    PRINT("the check's result", check_result);
    test_exit();
    return 0;
}
