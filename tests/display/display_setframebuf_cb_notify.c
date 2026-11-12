// A callback notified while a thread waits in sceDisplayWaitSetFrameBufCB or
// sceDisplayWaitSetFrameBufMultiCB. Whether it runs before the wait returns, and whether the wait
// still ends at the vblank it is for. The framebuffer is a black 960 by 544 one in CDRAM, like in
// the display test. The waiter W creates its callback, waits for a vblank without callbacks, sets
// the framebuffer for the next frame, and waits with callbacks for the given number of vblanks
// after the set. The main thread waits until the given number of vblanks passed since W's wait
// began and notifies the callback. The callback counts its runs and records the vblanks since W's
// wait began at its first run. After the wait W records its result, the vblanks since it began and
// the runs of the callback, then calls sceKernelCheckCallback and records its result and the runs
// after it. Each case has a fresh W and callback. The cases:
// - 1 vblank: sceDisplayWaitSetFrameBufCB, notified at once
// - 4 vblanks: sceDisplayWaitSetFrameBufMultiCB with 4, notified after 2 vblanks
// The block stays allocated until the process ends, like in the display test, since the display
// still shows the framebuffer in it.
#include <psp2/kernel/sysmem.h>

#include "vblank_cb.h"

#define WIDTH 960
#define HEIGHT 544
#define BUF_SIZE 0x200000

static int vblanks;
static void *buf;
static SceUID callback;
static volatile int ready, done, v0;
static volatile int runs, first_run_vblanks;
static volatile int set_result, wait_result, wait_vblanks, runs_in_wait, check_result, runs_after_check;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    if (runs == 0)
        first_run_vblanks = vblanks_since(v0);
    runs++;
    return 0;
}

static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    align_to_vblank();
    SceDisplayFrameBuf fb = { sizeof(SceDisplayFrameBuf), buf, WIDTH, SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, WIDTH, HEIGHT };
    set_result = sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    v0 = sceDisplayGetVcount();
    ready = 1;
    wait_result = vblanks == 1 ? sceDisplayWaitSetFrameBufCB() : sceDisplayWaitSetFrameBufMultiCB(vblanks);
    wait_vblanks = vblanks_since(v0);
    runs_in_wait = runs;
    check_result = sceKernelCheckCallback();
    runs_after_check = runs;
    sceKernelDeleteCallback(callback);
    done = 1;
    return 0;
}

static void run_case(const char *name, int count, int notify_at) {
    vblanks = count;
    ready = done = runs = 0;
    first_run_vblanks = NOT_DONE;
    set_result = wait_result = wait_vblanks = runs_in_wait = check_result = runs_after_check = NOT_DONE;
    const SceUID w = start_w(w_entry);
    TEST_WAIT_FOR(ready && in_cb_wait(w));
    const int wait_type = wait_type_of(w);
    TEST_WAIT_FOR(vblanks_since(v0) >= notify_at);
    const int notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(done);
    test_join_thread(w);

    PRINTF(set_result, "%s: the set", name);
    PRINTF(wait_type, "%s: W's wait type in the wait", name);
    PRINTF(notify, "%s: the notify", name);
    PRINTF(wait_result, "%s: the wait's result", name);
    PRINTF(runs_in_wait, "%s: runs of the callback when the wait returned", name);
    if (notify_at == 0)
        PRINTF(first_run_vblanks == 0, "%s: the callback first ran before the next vblank", name);
    else
        PRINTF(first_run_vblanks == notify_at, "%s: the callback first ran %d vblanks after the wait began", name, notify_at);
    PRINTF(wait_vblanks >= count, "%s: the wait returned at least %d vblank%s after it began", name, count, count == 1 ? "" : "s");
    PRINTF(wait_vblanks <= count + 1, "%s: the wait returned at most %d vblanks after it began", name, count + 1);
    PRINTF(check_result, "%s: the check's result", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
}

int main(void) {
    pin_main();
    const SceUID block = sceKernelAllocMemBlock("fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, BUF_SIZE, NULL);
    PRINT("allocate the framebuffer block", test_created(block));
    sceKernelGetMemBlockBase(block, &buf);
    sceClibMemset(buf, 0, BUF_SIZE);

    run_case("1 vblank", 1, 0);
    run_case("4 vblanks", 4, 2);
    test_exit();
    return 0;
}
