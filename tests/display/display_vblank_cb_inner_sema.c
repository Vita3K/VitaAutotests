// A callback that a thread runs in sceDisplayWaitVblankStartCB blocks on a semaphore, which the
// main thread signals. The waiter W creates its callback, waits for a vblank without callbacks and
// then for the next one with callbacks. The main thread notifies the callback once W is in that
// wait. The callback waits on the semaphore without callbacks, with a 2 s timeout, and records the
// result and the vblanks since W's wait began when it returns. The main thread reads W's wait type
// once W waits on the semaphore, then signals it. W records its steps: 1 when the callback starts,
// 2 when its semaphore wait returned, 3 when the vblank wait returned. After the wait W records
// its result, the vblanks since it began, and the runs of the callback, then calls
// sceKernelCheckCallback and records its result. Each case has a fresh W, callback and semaphore.
// The cases:
// - signal at once: the main thread signals the semaphore as soon as the callback waits on it,
//   long before the vblank W's wait is for
// - signal after the target vblank: the main thread signals it once 2 vblanks passed since W's
//   wait began, so the callback ends after the vblank W's wait is for
#include "vblank_cb.h"

static SceUID callback, sema;
static volatile int ready, done, v0, in_callback;
static volatile int runs, inner_result, callback_end_vblanks;
static volatile int wait_result, wait_vblanks, runs_in_wait, check_result;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    step(1);
    in_callback = 1;
    SceUInt timeout = TEST_WAIT_US;
    inner_result = sceKernelWaitSema(sema, 1, &timeout);
    step(2);
    runs++;
    callback_end_vblanks = vblanks_since(v0);
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

static void run_case(const char *name, int signal_after) {
    reset_steps();
    ready = done = in_callback = runs = 0;
    inner_result = callback_end_vblanks = wait_result = wait_vblanks = runs_in_wait = check_result = NOT_DONE;
    int wait_type = NOT_DONE, inner_wait_type = NOT_DONE;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID w = start_w(w_entry);
    TEST_WAIT_FOR(ready && in_cb_wait(w));
    wait_type = wait_type_of(w);
    const int notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(in_callback && test_in_wait(w, WAIT_TYPE_SEMA, sema));
    inner_wait_type = wait_type_of(w);
    if (signal_after)
        TEST_WAIT_FOR(vblanks_since(v0) >= 2);
    const int signal = sceKernelSignalSema(sema, 1);
    TEST_WAIT_FOR(done);
    test_join_thread(w);
    sceKernelDeleteSema(sema);

    PRINTF(wait_type, "%s: W's wait type in the vblank wait", name);
    PRINTF(notify, "%s: the notify", name);
    PRINTF(inner_wait_type, "%s: W's wait type in the callback's wait", name);
    PRINTF(signal, "%s: the signal", name);
    PRINTF(inner_result, "%s: the callback's wait", name);
    PRINTF(wait_result, "%s: the vblank wait's result", name);
    PRINTF(runs_in_wait, "%s: runs of the callback when the vblank wait returned", name);
    print_steps(name);
    PRINTF(callback_end_vblanks >= 2, "%s: the callback ended at least 2 vblanks after the wait began", name);
    PRINTF(wait_vblanks - callback_end_vblanks == 0, "%s: the wait returned in the frame the callback ended in", name);
    PRINTF(wait_vblanks - callback_end_vblanks <= 1, "%s: the wait returned at most 1 vblank after the callback ended", name);
    PRINTF(check_result, "%s: the check's result", name);
}

int main(void) {
    pin_main();
    run_case("signal at once", 0);
    run_case("signal after the target vblank", 1);
    test_exit();
    return 0;
}
