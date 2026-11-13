// What registering the same callback twice with sceDisplayRegisterVblankStartCallback does, and
// what unregistering it once leaves. A helper thread T creates a callback. Then T loops on
// sceDisplayWaitVblankStartCB until it is told to stop. Every vblank notifies the registered
// callback. The callback records its runs, the smallest, the largest and the last count it got
// and whether every notify id it got was -1. Each phase makes one register or unregister call.
// After the call has returned, T counts 6 returns of its wait that begin after the call. T reads
// the vblank count at the start of the first counted wait and at the sixth return, and copies
// the stats into a snapshot at that return. The main thread only coordinates. It prints nothing
// while a phase is measured and prints all snapshots after T was stopped and joined. The phases:
// - registered once
// - registered a second time
// - unregistered once
// - unregistered a second time
// - unregistered a third time, with nothing registered and no measurement
// For each phase the call's result is printed, and for the measured ones the runs as relations to
// the vblanks T counted, the smallest, the largest and the last count and whether every notify id
// was -1. The whole sequence runs in two variants. In "from main" the main thread makes the calls
// on T's callback. In "from owner" T makes them, told by the main thread, so the callback's owner
// makes every call. The difference between the variants shows whether a call from another thread
// works at all. After the variants the main thread registers and unregisters a semaphore UID,
// which is not a callback, and registers and unregisters 0. The main thread is pinned to core 0
// at 0x48 and T runs at 0xB0 on the same core.
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

#define PHASES 5

typedef struct Snapshot {
    int call_result, measured;
    int runs, lo, hi, last, ids, vblanks;
} Snapshot;

static SceUID callback;
static volatile int ready, stop, armed, counting, counted, done_phases;
static volatile int cmd, cmd_measure, cmd_ret;
static volatile int runs, min_count, max_count, last_count, all_ids_minus_1;
static volatile int v_start;
static Snapshot snaps[2][PHASES];
static volatile int variant_index;

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

static void start_counting(void) {
    runs = last_count = 0;
    min_count = NOT_DONE;
    max_count = 0;
    all_ids_minus_1 = 1;
    counted = 0;
    v_start = sceDisplayGetVcount();
    counting = 1;
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
        if (stop)
            break;
        if (counting) {
            counted++;
            if (counted == PHASE_VBLANKS) {
                Snapshot *snap = &snaps[variant_index][done_phases];
                snap->measured = 1;
                snap->runs = runs;
                snap->lo = min_count;
                snap->hi = max_count;
                snap->last = last_count;
                snap->ids = all_ids_minus_1;
                snap->vblanks = (sceDisplayGetVcount() - v_start) & 0xFFFF;
                counting = 0;
                done_phases++;
            }
        } else if (armed) {
            armed = 0;
            start_counting();
        }
        if (cmd != OP_NONE) {
            cmd_ret = do_op(cmd);
            if (cmd_measure)
                start_counting();
            cmd = OP_NONE;
        }
    }
    sceKernelDeleteCallback(callback);
    return 0;
}

static void phase(int by_owner, int index, int op, int measure) {
    int result;
    if (by_owner) {
        cmd_measure = measure;
        cmd = op;
        TEST_WAIT_FOR(cmd == OP_NONE);
        result = cmd_ret;
    } else {
        result = do_op(op);
        if (measure)
            armed = 1;
    }
    snaps[variant_index][index].call_result = result;
    if (measure)
        TEST_WAIT_FOR(done_phases == index + 1);
    else
        done_phases++;
}

static void run_variant(int by_owner) {
    ready = stop = armed = counting = done_phases = 0;
    cmd = OP_NONE;
    variant_index = by_owner;
    const SceUID t = start_w(t_entry);
    TEST_WAIT_FOR(ready);

    phase(by_owner, 0, OP_REGISTER, 1);
    phase(by_owner, 1, OP_REGISTER, 1);
    phase(by_owner, 2, OP_UNREGISTER, 1);
    phase(by_owner, 3, OP_UNREGISTER, 1);
    phase(by_owner, 4, OP_UNREGISTER, 0);

    stop = 1;
    test_join_thread(t);
}

static void print_variant(const char *variant, int index) {
    static const char *const names[PHASES] = { "registered once", "registered twice", "unregistered once", "unregistered twice", "unregistered three times" };
    for (int i = 0; i < PHASES; i++) {
        const Snapshot *s = &snaps[index][i];
        const char *name = names[i];
        PRINTF(s->call_result, "%s: %s: the call's result", variant, name);
        if (!s->measured)
            continue;
        PRINTF(s->runs == s->vblanks, "%s: %s: runs equal the vblanks", variant, name);
        PRINTF(s->runs > s->vblanks, "%s: %s: more runs than vblanks", variant, name);
        PRINTF(s->runs == 0, "%s: %s: no runs", variant, name);
        PRINTF(s->lo, "%s: %s: smallest count", variant, name);
        PRINTF(s->hi, "%s: %s: largest count", variant, name);
        PRINTF(s->last, "%s: %s: last count", variant, name);
        PRINTF(s->ids, "%s: %s: every notify id was -1", variant, name);
    }
}

int main(void) {
    pin_main();
    run_variant(0);
    run_variant(1);
    print_variant("from main", 0);
    print_variant("from owner", 1);

    const SceUID sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    PRINT("register a semaphore", sceDisplayRegisterVblankStartCallback(sema));
    PRINT("unregister a semaphore", sceDisplayUnregisterVblankStartCallback(sema));
    sceKernelDeleteSema(sema);
    PRINT("register 0", sceDisplayRegisterVblankStartCallback(0));
    PRINT("unregister 0", sceDisplayUnregisterVblankStartCallback(0));
    test_exit();
    return 0;
}
