// What registering the same callback twice with sceDisplayRegisterVblankStartCallback does, and
// what unregistering it once leaves. A helper thread T creates a callback and registers it once,
// or leaves that to the caller as the variant says. Then T loops on sceDisplayWaitVblankStartCB
// until it is told to stop. Every vblank notifies the registered callback. The callback records
// its runs, the smallest, the largest and the last count it got and whether every notify id it
// got was -1. Each phase makes one register or unregister call, then measures over the next 6
// returns of T's wait. The stats are reset together with the call, while T cannot run a callback,
// so a phase counts exactly 6 vblanks. The phases:
// - registered once
// - registered a second time
// - unregistered once
// - unregistered a second time
// - unregistered a third time, with nothing registered and no measurement
// For each phase the call's result is printed, and for the measured ones the runs as relations to
// 6, the smallest, the largest and the last count and whether every notify id was -1. The whole
// sequence runs in two variants. In "from main" the main thread makes the calls on T's callback.
// In "from owner" T makes them, told by the main thread, so the callback's owner makes every call.
// The difference between the variants shows whether a call from another thread works at all.
// After the variants the main thread registers and unregisters a semaphore UID, which is not a
// callback, and registers and unregisters 0. The main thread is pinned to core 0 at 0x48 and T
// runs at 0xB0 on the same core.
//
// On a PS Vita TV on firmware 3.65 a second registration of the same callback succeeds, and each
// run of the callback then gets a count of 2. One unregister brings the count back to 1, a second
// one stops the runs, and a third gives 0x800280A3. The notify id is always -1. It makes no
// difference whether the owner or another thread makes the calls. A semaphore gives 0x800280A1 to
// the register and 0x800280A3 to the unregister, and 0 gives 0x800280A1 to both.
#include "vblank_cb.h"

#define PHASE_VBLANKS 6
#define OP_NONE 0
#define OP_REGISTER 1
#define OP_UNREGISTER 2

static SceUID callback;
static volatile int ready, stop, returns, base;
static volatile int cmd, cmd_ret;
static volatile int runs, min_count, max_count, last_count, all_ids_minus_1;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)arg;
    (void)common;
    if (count < min_count)
        min_count = count;
    if (count > max_count)
        max_count = count;
    last_count = count;
    if (notify_id != -1)
        all_ids_minus_1 = 0;
    runs++;
    return 0;
}

static void reset_stats(void) {
    runs = last_count = 0;
    min_count = NOT_DONE;
    max_count = 0;
    all_ids_minus_1 = 1;
    base = returns;
}

static int do_op(int op) {
    return op == OP_REGISTER ? sceDisplayRegisterVblankStartCallback(callback) : sceDisplayUnregisterVblankStartCallback(callback);
}

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("vblank", 0, callback_entry, NULL);
    ready = 1;
    while (!stop) {
        sceDisplayWaitVblankStartCB();
        returns++;
        if (cmd != OP_NONE) {
            reset_stats();
            cmd_ret = do_op(cmd);
            cmd = OP_NONE;
        }
    }
    sceKernelDeleteCallback(callback);
    return 0;
}

// Makes the call from the thread the variant says and returns its result
static int call(int by_owner, int op) {
    if (!by_owner) {
        reset_stats();
        return do_op(op);
    }
    cmd = op;
    TEST_WAIT_FOR(cmd == OP_NONE);
    return cmd_ret;
}

static void phase(const char *variant, const char *name, int by_owner, int op, int measure) {
    const int result = call(by_owner, op);
    PRINTF(result, "%s: %s: the call's result", variant, name);
    if (!measure)
        return;
    TEST_WAIT_FOR(returns >= base + PHASE_VBLANKS);
    const int n = runs, lo = min_count, hi = max_count, last = last_count, ids = all_ids_minus_1;
    PRINTF(n == PHASE_VBLANKS, "%s: %s: runs equal the vblanks", variant, name);
    PRINTF(n > PHASE_VBLANKS, "%s: %s: more runs than vblanks", variant, name);
    PRINTF(n == 0, "%s: %s: no runs", variant, name);
    PRINTF(lo, "%s: %s: smallest count", variant, name);
    PRINTF(hi, "%s: %s: largest count", variant, name);
    PRINTF(last, "%s: %s: last count", variant, name);
    PRINTF(ids, "%s: %s: every notify id was -1", variant, name);
}

static void run_variant(const char *variant, int by_owner) {
    ready = stop = returns = 0;
    cmd = OP_NONE;
    const SceUID t = start_w(t_entry);
    TEST_WAIT_FOR(ready && returns >= 1);

    phase(variant, "registered once", by_owner, OP_REGISTER, 1);
    phase(variant, "registered twice", by_owner, OP_REGISTER, 1);
    phase(variant, "unregistered once", by_owner, OP_UNREGISTER, 1);
    phase(variant, "unregistered twice", by_owner, OP_UNREGISTER, 1);
    phase(variant, "unregistered three times", by_owner, OP_UNREGISTER, 0);

    stop = 1;
    test_join_thread(t);
}

int main(void) {
    pin_main();
    run_variant("from main", 0);
    run_variant("from owner", 1);

    const SceUID sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    PRINT("register a semaphore", sceDisplayRegisterVblankStartCallback(sema));
    PRINT("unregister a semaphore", sceDisplayUnregisterVblankStartCallback(sema));
    sceKernelDeleteSema(sema);
    PRINT("register 0", sceDisplayRegisterVblankStartCallback(0));
    PRINT("unregister 0", sceDisplayUnregisterVblankStartCallback(0));
    test_exit();
    return 0;
}
