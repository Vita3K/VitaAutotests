// A callback registered with sceDisplayRegisterVblankStartCallback, which every vblank notifies,
// while its thread waits in sceDisplayWaitVblankStartCB or sceDisplayWaitVblankStartMultiCB. The
// main thread creates the callback, waits for a vblank without callbacks, registers the callback
// and waits with callbacks for the given number of vblanks. The callback records, for each of its
// first runs, the count it got and the vblanks since the wait began. After the wait the main
// thread records its result, the vblanks since it began and the runs of the callback, unregisters
// the callback, calls sceKernelCheckCallback and records its result and the runs after it. The
// cases wait for 1 and for 3 vblanks, each with a fresh callback. The main thread is pinned to
// core 0 at 0x48.
#include "vblank_cb.h"

#define MAX_RUNS 8

static volatile int v0, runs;
static volatile int run_vblanks[MAX_RUNS], run_counts[MAX_RUNS];

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)arg;
    (void)common;
    if (runs < MAX_RUNS) {
        run_vblanks[runs] = vblanks_since(v0);
        run_counts[runs] = count;
    }
    runs++;
    return 0;
}

static void run_case(const char *name, int vblanks) {
    runs = 0;
    for (int i = 0; i < MAX_RUNS; i++)
        run_vblanks[i] = run_counts[i] = NOT_DONE;
    const SceUID callback = sceKernelCreateCallback("vblank", 0, callback_entry, NULL);
    v0 = align_to_vblank();
    const int reg = sceDisplayRegisterVblankStartCallback(callback);
    const int result = vblanks == 1 ? sceDisplayWaitVblankStartCB() : sceDisplayWaitVblankStartMultiCB(vblanks);
    const int wait_vblanks = vblanks_since(v0);
    const int runs_in_wait = runs;
    const int unreg = sceDisplayUnregisterVblankStartCallback(callback);
    const int check = sceKernelCheckCallback();
    const int runs_after_check = runs;
    sceKernelDeleteCallback(callback);

    PRINTF(reg, "%s: the register", name);
    PRINTF(result, "%s: the wait's result", name);
    PRINTF(wait_vblanks == vblanks, "%s: the wait returned at vblank %d after it began", name, vblanks);
    PRINTF(runs_in_wait, "%s: runs of the callback when the wait returned", name);
    PRINTF(unreg, "%s: the unregister", name);
    PRINTF(check, "%s: the check's result", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
    for (int i = 0; i < runs_after_check && i < MAX_RUNS; i++) {
        PRINTF(run_vblanks[i], "%s: run %d: vblanks since the wait began", name, i);
        PRINTF(run_counts[i], "%s: run %d: the count it got", name, i);
    }
}

int main(void) {
    pin_main();
    run_case("1 vblank", 1);
    run_case("3 vblanks", 3);
    test_exit();
    return 0;
}
