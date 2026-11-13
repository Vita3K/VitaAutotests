// What the waits for a framebuffer set do when the vblank they wait for has already come. The
// main thread owns a callback, notifies it and then calls the wait, so the callback is pending
// and has not run when the call starts. The call returns in the same vblank it was called in, in
// the next one or later. The framebuffer is a black 960 by 544 one in CDRAM, like in
// display_setframebuf_cb_notify. For each case the main thread records the wait's result, whether
// the callback had run when the wait returned and the vblank relation. Then it calls
// sceKernelCheckCallback and records its result and whether the callback has run after it. Each
// attempt sets the framebuffer for the next frame. In the passed cases it then waits for 3
// vblanks without callbacks, so the vblank of the set has passed. In the not passed cases it
// waits for one vblank and then makes the set and the wait right after it, so the wait has to
// wait for the vblank. The count is read after the 3 vblanks in the passed cases and before the
// set in the not passed cases, and again after the notify. An attempt where a vblank came in
// between is dropped. An attempt is tried up to 50 times and only the accepted
// one is printed, with caught 0 if there was none. The cases:
// - passed, CB: sceDisplayWaitSetFrameBufCB
// - not passed, CB: sceDisplayWaitSetFrameBufCB
// - passed, multi CB 1: sceDisplayWaitSetFrameBufMultiCB with 1
// - not passed, multi CB 1: sceDisplayWaitSetFrameBufMultiCB with 1
// - passed, multi CB 2: sceDisplayWaitSetFrameBufMultiCB with 2
// - passed, plain: sceDisplayWaitSetFrameBuf without callbacks as the control, nothing notified
// The block stays allocated until the process ends, since the display still shows the framebuffer
// in it. The main thread is pinned to core 0 at 0x48.
//
// On a PS Vita TV on firmware 3.65 a wait whose vblank already came returns in the vblank it was
// called in and leaves the callback pending, with and without the multi form. The check after it
// runs the callback. A wait that has to wait for its vblank runs the callback before it returns.
#include <psp2/kernel/sysmem.h>

#include "vblank_cb.h"

#define WIDTH 960
#define HEIGHT 544
#define BUF_SIZE 0x200000
#define ATTEMPTS 50

enum { WAIT_CB, WAIT_MULTI_1, WAIT_MULTI_2, WAIT_PLAIN };

static void *buf;
static volatile int runs;

struct result {
    int caught;
    int result;
    int ran_in_wait;
    int same, next, later;
    int check;
    int ran_after_check;
};

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    runs++;
    return 0;
}

static int do_wait(int wait) {
    switch (wait) {
    case WAIT_CB:
        return sceDisplayWaitSetFrameBufCB();
    case WAIT_MULTI_1:
        return sceDisplayWaitSetFrameBufMultiCB(1);
    case WAIT_MULTI_2:
        return sceDisplayWaitSetFrameBufMultiCB(2);
    default:
        return sceDisplayWaitSetFrameBuf();
    }
}

static void attempt(SceUID callback, int wait, int passed, struct result *r) {
    SceDisplayFrameBuf fb = { sizeof(SceDisplayFrameBuf), buf, WIDTH, SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, WIDTH, HEIGHT };
    for (int i = 0; i < ATTEMPTS; i++) {
        runs = 0;
        int v_a;
        if (passed) {
            sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
            for (int j = 0; j < 3; j++)
                sceDisplayWaitVblankStart();
            v_a = sceDisplayGetVcount();
        } else {
            sceDisplayWaitVblankStart();
            v_a = sceDisplayGetVcount();
            sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
        }
        if (wait != WAIT_PLAIN)
            sceKernelNotifyCallback(callback, 1);
        const int v_b = sceDisplayGetVcount();
        const int result = do_wait(wait);
        const int ran_in_wait = runs;
        const int v_c = sceDisplayGetVcount();
        const int check = sceKernelCheckCallback();
        const int ran_after_check = runs;
        if (v_a != v_b)
            continue;
        const int d = (v_c - v_b) & 0xFFFF;
        r->caught = 1;
        r->result = result;
        r->ran_in_wait = ran_in_wait;
        r->same = d == 0;
        r->next = d == 1;
        r->later = d >= 2;
        r->check = check;
        r->ran_after_check = ran_after_check;
        return;
    }
}

static void run_case(const char *name, int wait, int passed) {
    const SceUID callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    struct result r = { 0 };
    attempt(callback, wait, passed, &r);
    sceKernelDeleteCallback(callback);

    PRINTF(r.caught, "%s: caught", name);
    if (!r.caught)
        return;
    PRINTF(r.result, "%s: the wait's result", name);
    PRINTF(r.ran_in_wait, "%s: runs of the callback when the wait returned", name);
    PRINTF(r.same, "%s: returned in the same vblank", name);
    PRINTF(r.next, "%s: returned in the next vblank", name);
    PRINTF(r.later, "%s: returned later", name);
    PRINTF(r.check, "%s: the check's result", name);
    PRINTF(r.ran_after_check, "%s: runs of the callback after the check", name);
}

int main(void) {
    pin_main();
    const SceUID block = sceKernelAllocMemBlock("fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, BUF_SIZE, NULL);
    PRINT("allocate the framebuffer block", test_created(block));
    sceKernelGetMemBlockBase(block, &buf);
    sceClibMemset(buf, 0, BUF_SIZE);

    run_case("passed, CB", WAIT_CB, 1);
    run_case("not passed, CB", WAIT_CB, 0);
    run_case("passed, multi CB 1", WAIT_MULTI_1, 1);
    run_case("not passed, multi CB 1", WAIT_MULTI_1, 0);
    run_case("passed, multi CB 2", WAIT_MULTI_2, 1);
    run_case("passed, plain", WAIT_PLAIN, 1);
    test_exit();
    return 0;
}
