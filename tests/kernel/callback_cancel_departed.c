// What a callback of a thread T that waits with callbacks does when it is cancelled or deleted after
// its notify took T out of the wait, but before T ran a callback. Also what happens to the notified
// count and argument, and to a second callback notified at the same time. Every thread is pinned to
// core 0. The main thread runs at 0x60 and T at 0xB0, so T stays ready until the main thread blocks.
// T creates its callbacks in its own thread before it waits: a target and an other callback. Every
// wait has a timeout of a few seconds, and every case has fresh objects, a fresh T and fresh callbacks.
// The main thread waits until T waits, notifies the target and makes the calls of the case with no
// call that blocks in between. Then it waits until T waits again with the callback wait type, reads
// the callbacks' counts, ends T's wait and joins T, and prints. The prints come last, since a print
// blocks the main thread and lets T run. Cases, for each kind:
// - cancel: the target is notified, then cancelled
// - delete: the target is notified, then deleted, and it is notified once more after the delete
// - notify twice: the target is notified with 1, then with 2, cancelled, then notified with 3, so its
//   one run gets count 1 and argument 3
// - two callbacks: the target and the other are notified, then the target is cancelled, so only the
//   other runs
// The kinds: a semaphore at 0, a delay with callbacks of 3 s, which nothing ends early, a thread
// signal wait, ended by sceKernelSendSignal, and a thread end wait, ended when a thread that waits
// on a gate returns.
// Each case prints the results of its calls, the target's and the other's runs and notification
// counts, the count and argument the target's run got, whether T waits again with the callback wait
// type of its kind, and the result of T's wait.
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define LONG_TIMEOUT (3 * TEST_WAIT_US)
#define DELAY_US 3000000

typedef enum Kind {
    SEMA,
    DELAY,
    SIGNAL,
    THREAD_END
} Kind;

typedef enum Sub {
    CANCEL,
    DELETE,
    NOTIFY_TWICE,
    TWO_CALLBACKS
} Sub;

static const char *const kind_names[] = { "semaphore", "delay", "signal", "thread end" };
static const char *const sub_names[] = { "cancel", "delete", "notify twice, cancel, notify once",
                                         "two callbacks, cancel the target" };

// What a callback counts: its runs, and the count and argument it got at its last run
typedef struct Probe {
    int runs;
    int count;
    int arg;
} Probe;

static Kind kind;
static Sub sub;
static SceUID waiter_thread, ender, gate, target_cb, other_cb;
// probes[0] belongs to target_cb, probes[1] to other_cb
static Probe probes[2];
static volatile int result;

static int probe_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    Probe *probe = common;
    probe->runs++;
    probe->count = count;
    probe->arg = arg;
    return 0;
}

// Waits for the gate, then returns, so the thread end wait ends
static int ender_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = LONG_TIMEOUT;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// Creates its callbacks, then waits with callbacks in the kind of the current case
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = LONG_TIMEOUT;
    target_cb = sceKernelCreateCallback("target", 0, probe_callback, &probes[0]);
    other_cb = sceKernelCreateCallback("other", 0, probe_callback, &probes[1]);
    switch (kind) {
    case SEMA: result = sceKernelWaitSemaCB(gate, 1, &timeout); break;
    case DELAY: result = sceKernelDelayThreadCB(DELAY_US); break;
    case SIGNAL: result = sceKernelWaitSignalCB(0, 0, &timeout); break;
    case THREAD_END: result = sceKernelWaitThreadEndCB(ender, NULL, &timeout); break;
    }
    return 0;
}

// The wait type of the kind, with the callback bit
static SceUInt32 wait_type_of(Kind k) {
    switch (k) {
    case SEMA: return WAIT_TYPE_SEMA | WAIT_TYPE_CB;
    case DELAY: return WAIT_TYPE_DELAY | WAIT_TYPE_CB;
    case SIGNAL: return WAIT_TYPE_SIGNAL | WAIT_TYPE_CB;
    case THREAD_END: return WAIT_TYPE_THREAD_END | WAIT_TYPE_CB;
    }
    return 0;
}

// Returns whether T waits with the callback wait type of the kind
static int waits(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(waiter_thread, &info) == 0 && info.waitType == wait_type_of(kind);
}

// Creates the gate, and for a thread end wait the thread that waits on the gate
static void create_objects(void) {
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    if (kind == THREAD_END) {
        ender = sceKernelCreateThread("ender", ender_entry, WAITER_PRIORITY, 0x4000, 0,
                                      SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(ender, 0, NULL);
    }
}

// Ends T's wait, and returns the result of the call that does it, or NOT_DONE if nothing does
static int end_wait(void) {
    switch (kind) {
    case SEMA:
    case THREAD_END: return sceKernelSignalSema(gate, 1);
    case SIGNAL: return sceKernelSendSignal(waiter_thread);
    case DELAY: break;
    }
    return NOT_DONE;
}

static void run_case(Kind k, Sub s) {
    char name[64];
    sceClibSnprintf(name, sizeof(name), "%s, %s", kind_names[k], sub_names[s]);
    kind = k;
    sub = s;
    result = NOT_DONE;
    probes[0] = (Probe) { 0 };
    probes[1] = (Probe) { 0 };
    target_cb = other_cb = -1;
    create_objects();
    waiter_thread = sceKernelCreateThread("t", waiter, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0,
                                          NULL);
    sceKernelStartThread(waiter_thread, 0, NULL);
    TEST_WAIT_FOR(waits());

    // Nothing between the notifies and the cancel, delete or extra notify blocks
    int notify[3] = { NOT_DONE, NOT_DONE, NOT_DONE };
    int cancel = NOT_DONE, delete_result = NOT_DONE;
    switch (s) {
    case CANCEL:
        notify[0] = sceKernelNotifyCallback(target_cb, 1);
        cancel = sceKernelCancelCallback(target_cb);
        break;
    case DELETE:
        notify[0] = sceKernelNotifyCallback(target_cb, 1);
        delete_result = sceKernelDeleteCallback(target_cb);
        break;
    case NOTIFY_TWICE:
        notify[0] = sceKernelNotifyCallback(target_cb, 1);
        notify[1] = sceKernelNotifyCallback(target_cb, 2);
        cancel = sceKernelCancelCallback(target_cb);
        notify[2] = sceKernelNotifyCallback(target_cb, 3);
        break;
    case TWO_CALLBACKS:
        notify[0] = sceKernelNotifyCallback(target_cb, 1);
        notify[1] = sceKernelNotifyCallback(other_cb, 1);
        cancel = sceKernelCancelCallback(target_cb);
        break;
    }

    // Waits until T ran what it had pending and waits again
    TEST_WAIT_FOR(waits());
    const int waits_again = waits();
    int notify_after_delete = NOT_DONE;
    if (s == DELETE)
        notify_after_delete = sceKernelNotifyCallback(target_cb, 1);
    // The callbacks die with T, so their counts are read before T's wait ends
    const int target_left = sceKernelGetCallbackCount(target_cb);
    const int other_left = sceKernelGetCallbackCount(other_cb);

    const int end = end_wait();
    test_join_thread(waiter_thread);
    if (k == THREAD_END)
        test_join_thread(ender);

    switch (s) {
    case CANCEL:
        PRINTF(notify[0], "%s: notify of the target", name);
        PRINTF(cancel, "%s: cancel of the target", name);
        break;
    case DELETE:
        PRINTF(notify[0], "%s: notify of the target", name);
        PRINTF(delete_result, "%s: delete of the target", name);
        break;
    case NOTIFY_TWICE:
        PRINTF(notify[0], "%s: notify of the target with 1", name);
        PRINTF(notify[1], "%s: notify of the target with 2", name);
        PRINTF(cancel, "%s: cancel of the target", name);
        PRINTF(notify[2], "%s: notify of the target with 3", name);
        break;
    case TWO_CALLBACKS:
        PRINTF(notify[0], "%s: notify of the target", name);
        PRINTF(notify[1], "%s: notify of the other", name);
        PRINTF(cancel, "%s: cancel of the target", name);
        break;
    }
    PRINTF(probes[0].runs, "%s: runs of the target", name);
    PRINTF(probes[1].runs, "%s: runs of the other", name);
    PRINTF(target_left, "%s: the target's notifications left", name);
    PRINTF(other_left, "%s: the other's notifications left", name);
    if (s == NOTIFY_TWICE) {
        PRINTF(probes[0].count, "%s: the count the target's run got", name);
        PRINTF(probes[0].arg, "%s: the argument the target's run got", name);
    }
    if (s == DELETE)
        PRINTF(notify_after_delete, "%s: notify of the target after the delete", name);
    PRINTF(waits_again, "%s: T waits again with the callback wait type", name);
    if (k != DELAY)
        PRINTF(end, "%s: the call that ends T's wait", name);
    PRINTF(result, "%s: T's wait result", name);

    sceKernelDeleteSema(gate);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    for (int k = SEMA; k <= THREAD_END; k++)
        for (int s = CANCEL; s <= TWO_CALLBACKS; s++)
            run_case(k, s);

    test_exit();
    return 0;
}
