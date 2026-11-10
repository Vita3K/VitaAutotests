// A set with notification on a simple event E reaches the callback of a thread T that waits with
// callbacks in sceKernelWaitMultipleEventsCB. T creates its callback, registers it to E and waits
// in OR mode with a timeout of 6 seconds. Once T waits, the main thread sets E with notification
// and reads what T and the events show right after it, with no call that can block in between.
// Then it waits until the callback ran and, where T should wait again, until it does, releases T
// with a set of F, joins it and prints. The main thread runs at 0x60 and T at 0xB0, both pinned to
// core 0, so T only runs while the main thread blocks. Each case has fresh events, F and G with
// manual reset. The cases, the wait that completes first:
// - T waits on E bit A and F bit A, and the notifying set of E bit A completes the wait
// - T waits on F bit A and G bit A, so the callback's event isn't one of the waited events
// - T waits on E bit A and F bit A, and the notifying set gives E bit B, which T doesn't wait for
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define LONG_US (3 * TEST_WAIT_US)

typedef enum Kind {
    COMPLETES,
    NOT_WAITED,
    OTHER_BIT
} Kind;

static SceUID e, f, g;
static volatile SceUID callback;
static volatile int callback_runs, got_notify_id, got_count, got_arg;
static volatile int wait_result, register_result, runs_at_return;
static Kind current;
static WaitEvent list[2];
static ResultEvent results[2];

static int notified_callback(int notify_id, int count, int arg, void *common) {
    (void)common;
    callback_runs++;
    got_notify_id = notify_id;
    got_count = count;
    got_arg = arg;
    return 0;
}

// Creates its callback, registers it to E and waits on the list of the case
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("notified", 0, notified_callback, NULL);
    register_result = sceKernelRegisterCallbackToEvent(e, callback);
    SceUInt32 timeout = LONG_US;
    wait_result = sceKernelWaitMultipleEventsCB(list, 2, EVENT_WAIT_MODE_OR, results, &timeout);
    runs_at_return = callback_runs;
    sceKernelDeleteCallback(callback);
    return 0;
}

static int waiting_threads(SceUID event) {
    EventInfo info = { .size = sizeof(info) };
    return sceKernelGetEventInfo(event, &info) == 0 ? (int)info.numWaitThreads : NOT_DONE;
}

static SceKernelThreadInfo info_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    if (sceKernelGetThreadInfo(thread, &info) != 0)
        info.status = info.waitType = NOT_DONE;
    return info;
}

static int waits_multi_cb(SceUID thread) {
    return info_of(thread).waitType == (WAIT_TYPE_MP_EVENTS | WAIT_TYPE_CB);
}

// Returns whether thread has ended, which T only does after its wait returned
static int ended(SceUID thread) {
    return info_of(thread).status == SCE_THREAD_DORMANT;
}

static void run_case(Kind kind, const char *name) {
    current = kind;
    e = sceKernelCreateSimpleEvent("e", EVENT_ATTR_NOTIFY_CB_ALL, 0, NULL);
    f = sceKernelCreateSimpleEvent("f", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    g = sceKernelCreateSimpleEvent("g", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    list[0] = (WaitEvent){ kind == NOT_WAITED ? f : e, EVENT_BIT_A };
    list[1] = (WaitEvent){ kind == NOT_WAITED ? g : f, EVENT_BIT_A };
    callback = 0;
    callback_runs = runs_at_return = 0;
    got_notify_id = got_count = got_arg = NOT_DONE;
    wait_result = register_result = NOT_DONE;
    const SceUID thread = sceKernelCreateThread("t", waiter, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    // T can't end before it waits, and its wait only returns after a set below or its timeout
    TEST_WAIT_FOR(waits_multi_cb(thread) || ended(thread));

    const int notify = sceKernelSetEventWithNotifyCallback(e, kind == OTHER_BIT ? EVENT_BIT_B : EVENT_BIT_A, 0, 0x55);
    const int e_waiting = waiting_threads(e), f_waiting = waiting_threads(f), g_waiting = waiting_threads(g);
    const int pending = sceKernelGetCallbackCount(callback);
    const SceKernelThreadInfo after = info_of(thread);

    // Waits for the callback, and where T should wait again, for that, at most 2 seconds. It also
    // stops once T ended, since then neither can change anymore.
    const SceUInt64 end = sceKernelGetProcessTimeWide() + 2000000;
    while (!(callback_runs > 0 && (kind == COMPLETES || waits_multi_cb(thread))) && !ended(thread) && sceKernelGetProcessTimeWide() < end)
        sceKernelDelayThread(1000);
    const int runs = callback_runs;
    const int waits_again = kind != COMPLETES && waits_multi_cb(thread);
    const int e_again = waiting_threads(e), f_again = waiting_threads(f), g_again = waiting_threads(g);
    const int release = kind == COMPLETES ? 0 : sceKernelSetEvent(f, EVENT_BIT_A, 0);
    SceUInt timeout = LONG_US;
    const int join = sceKernelWaitThreadEnd(thread, NULL, &timeout);

    PRINTF(register_result, "%s: register the callback to E", name);
    PRINTF(notify, "%s: the notify", name);
    PRINTF(e_waiting, "%s: E's waiting threads after the notify", name);
    PRINTF(f_waiting, "%s: F's waiting threads after the notify", name);
    if (kind == NOT_WAITED)
        PRINTF(g_waiting, "%s: G's waiting threads after the notify", name);
    PRINTF(pending, "%s: the callback's notifications after the notify", name);
    PRINTF(after.status, "%s: T's status after the notify", name);
    PRINTF(after.waitType, "%s: T's wait type after the notify", name);
    PRINTF(runs, "%s: runs of the callback", name);
    PRINTF(got_notify_id == e, "%s: the notify ID is E", name);
    PRINTF(got_count, "%s: the count", name);
    PRINTF(got_arg, "%s: the argument", name);
    if (kind != COMPLETES) {
        PRINTF(waits_again, "%s: T waits again", name);
        PRINTF(e_again, "%s: E's waiting threads then", name);
        PRINTF(f_again, "%s: F's waiting threads then", name);
        if (kind == NOT_WAITED)
            PRINTF(g_again, "%s: G's waiting threads then", name);
        PRINTF(release, "%s: the release", name);
    }
    PRINTF(join, "%s: the join", name);
    PRINTF(wait_result, "%s: T's wait result", name);
    PRINTF(runs_at_return, "%s: runs of the callback when the wait returned", name);

    sceKernelDeleteThread(thread);
    sceKernelDeleteSimpleEvent(e);
    sceKernelDeleteSimpleEvent(f);
    sceKernelDeleteSimpleEvent(g);
}

int main(void) {
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelDelayThread(1000);
    run_case(COMPLETES, "the set completes the wait");
    run_case(NOT_WAITED, "the callback's event isn't waited");
    run_case(OTHER_BIT, "the callback's event is waited for another bit");
    test_exit();
    return 0;
}
