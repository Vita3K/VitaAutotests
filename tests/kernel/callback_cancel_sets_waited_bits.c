// A cancel that sets the bit a waiter waits for while the waiter runs its callback, and a set then
// a clear in the same callback. Each case has a fresh object, a fresh gate semaphore and a fresh
// waiter T. T waits with callbacks for bit 0x1 of an event flag, or for EVENT_BIT_A of a simple
// event, and its callback counts its run and then blocks on the gate with a timeout of 6 seconds.
// The main thread notifies the callback once T waits, and waits until the callback blocks on the
// gate. Then it makes the call of the case while the callback blocks, and reads T's status and the
// call's results. It signals the gate, waits until T's wait ended, or until T waits again, and
// joins T. Every thread is pinned to core 0. The main thread runs at 0x60 and T and the queued
// waiter of one case at 0xB0, so they only run while the main thread blocks. The cases:
// - a manual reset simple event, CancelEventWithSetPattern with the pattern 0x99000000 and user
//   data 0x77. 0x99 in the top byte is the user bits, which include EVENT_BIT_A.
// - an auto reset simple event, the same cancel
// - an event flag without SCE_EVENT_WAITMULTIPLE, CancelEventFlag to bit 0x1, and T's retry takes
//   the bit without clearing it
// - the same flag, with SCE_EVENT_WAITCLEAR in T's wait
// - an event flag with SCE_EVENT_WAITMULTIPLE and a second waiter Q queued behind T for bit 0x2,
//   CancelEventFlag to bit 0x1. Q's wait is ended by the cancel, and T's retry takes bit 0x1.
// - a manual reset simple event, where the callback sets EVENT_BIT_A and then clears every bit with
//   ClearEvent of mask 0. T's retry finds nothing and waits again, and the main thread sets
//   EVENT_BIT_A with user data 0x66 to end that wait.
// Each case prints the notify call's result and the callback's runs, the cancel's or the set's and
// clear's results, the cancel's count of woken waiters, T's status during the callback, and then
// T's wait result, the bits and user data it got, and the pattern after its wait.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define LONG_US (3 * TEST_WAIT_US)
// The pattern of the simple event cancels. Its top byte holds the user bits, and 0x99 includes
// EVENT_BIT_A there.
#define CANCEL_PATTERN 0x99000000
#define CANCEL_USER_DATA 0x77
#define SET_USER_DATA 0x88
#define RELEASE_USER_DATA 0x66

typedef enum Case {
    SIMPLE_MANUAL,
    SIMPLE_AUTO,
    FLAG_SINGLE,
    FLAG_SINGLE_CLEAR,
    FLAG_MULTI_QUEUED,
    SIMPLE_SET_CLEAR
} Case;

static SceUID object, gate;
static volatile SceUID callback;
static Case current_case;
static volatile int callback_runs, done, wait_result, queued_result;
static volatile SceUInt32 wait_bits;
static volatile SceUInt64 wait_user_data;

// Counts its run, then blocks on the gate until the main thread signals it
static int gate_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    SceUInt timeout = LONG_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// Returns whether the case waits on a simple event rather than an event flag
static int is_simple(Case c) {
    return c == SIMPLE_MANUAL || c == SIMPLE_AUTO || c == SIMPLE_SET_CLEAR;
}

// Creates its callback, then waits with callbacks for bit 0x1 of the flag or for EVENT_BIT_A
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("gate", 0, gate_callback, NULL);
    SceUInt timeout = LONG_US;
    SceUInt32 bits = 0;
    SceUInt64 user_data = 0;
    int result;
    if (is_simple(current_case))
        result = sceKernelWaitEventCB(object, EVENT_BIT_A, &bits, &user_data, &timeout);
    else
        result = sceKernelWaitEventFlagCB(object, 0x1, SCE_EVENT_WAITOR | (current_case == FLAG_SINGLE_CLEAR ? SCE_EVENT_WAITCLEAR : 0), &bits, &timeout);
    wait_result = result;
    wait_bits = bits;
    wait_user_data = user_data;
    done = 1;
    sceKernelDeleteCallback(callback);
    return 0;
}

// Waits on the flag for bit 0x2, which is never set, so only the cancel ends this wait
static int queued_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 bits = 0;
    SceUInt timeout = LONG_US;
    queued_result = sceKernelWaitEventFlag(object, 0x2, SCE_EVENT_WAITOR, &bits, &timeout);
    return 0;
}

// Creates the object of the case and the gate, and starts T on core 0 and waits until T waits
// with callbacks on the object
static SceUID start_waiter(Case c) {
    current_case = c;
    callback = 0;
    callback_runs = done = 0;
    wait_result = queued_result = NOT_DONE;
    wait_bits = NOT_DONE;
    wait_user_data = NOT_DONE;
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    switch (c) {
    case SIMPLE_MANUAL:
    case SIMPLE_SET_CLEAR: object = sceKernelCreateSimpleEvent("event", EVENT_ATTR_MANUAL_RESET, 0, NULL); break;
    case SIMPLE_AUTO: object = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL); break;
    case FLAG_SINGLE:
    case FLAG_SINGLE_CLEAR: object = sceKernelCreateEventFlag("flag", 0, 0, NULL); break;
    case FLAG_MULTI_QUEUED: object = sceKernelCreateEventFlag("flag", SCE_EVENT_WAITMULTIPLE, 0, NULL); break;
    }
    const SceUID thread = sceKernelCreateThread("t", waiter, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    const SceUInt32 type = (is_simple(c) ? WAIT_TYPE_EVENT : WAIT_TYPE_EVENT_FLAG) | WAIT_TYPE_CB;
    TEST_WAIT_FOR(test_in_wait(thread, type, object));
    return thread;
}

// Notifies the callback of T, then waits until the callback blocks on the gate. Returns the
// result of the notify.
static int notify_and_block(SceUID thread) {
    const int notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_SEMA, gate));
    return notify;
}

// Returns the pattern of the object of the case
static SceUInt32 pattern_of(Case c) {
    SceUInt32 pattern = NOT_DONE;
    if (is_simple(c)) {
        sceKernelGetEventPattern(object, &pattern);
    } else {
        SceKernelEventFlagInfo info = { .size = sizeof(info) };
        if (sceKernelGetEventFlagInfo(object, &info) == 0)
            pattern = info.currentPattern;
    }
    return pattern;
}

// Returns the status of thread, which is 2 when it is ready and 8 when it waits
static int status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? (int)info.status : NOT_DONE;
}

// Cancels the wait of the case with a set pattern while the callback blocks, then lets T's
// retry take what the cancel set
static void cancel_case(Case c, const char *name) {
    const SceUID thread = start_waiter(c);
    SceUID queued = 0;
    const int notify = notify_and_block(thread);
    const int runs = callback_runs;
    if (c == FLAG_MULTI_QUEUED) {
        queued = sceKernelCreateThread("q", queued_waiter, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(queued, 0, NULL);
        TEST_WAIT_FOR(test_in_wait(queued, WAIT_TYPE_EVENT_FLAG, object));
    }
    SceUInt32 woken = NOT_DONE;
    const int cancel = is_simple(c) ? sceKernelCancelEventWithSetPattern(object, CANCEL_PATTERN, CANCEL_USER_DATA, &woken)
                                    : sceKernelCancelEventFlag(object, 0x1, &woken);
    const int status = status_of(thread);
    sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(done);
    test_join_thread(thread);
    if (queued)
        test_join_thread(queued);
    const SceUInt32 pattern = pattern_of(c);
    if (is_simple(c))
        sceKernelDeleteSimpleEvent(object);
    else
        sceKernelDeleteEventFlag(object);
    sceKernelDeleteSema(gate);

    PRINTF(notify, "%s: the notify", name);
    PRINTF(runs, "%s: runs of the callback before the cancel", name);
    PRINTF(cancel, "%s: the cancel", name);
    PRINTF(woken, "%s: the waiters the cancel woke", name);
    PRINTF(status, "%s: T's status during the callback", name);
    if (queued)
        PRINTF(queued_result, "%s: the queued waiter's result", name);
    PRINTF(wait_result, "%s: T's wait result", name);
    PRINTF(wait_bits, "%s: the bits T's wait got", name);
    if (is_simple(c))
        PRINTF((int)wait_user_data, "%s: the user data low word T's wait got", name);
    PRINTF(pattern, "%s: the pattern after", name);
}

// Sets EVENT_BIT_A and clears every bit during the callback. T's retry finds nothing and waits
// again, and the main thread sets the bit again to end that wait.
static void set_clear_case(void) {
    const char *name = "a simple event set then cleared";
    const SceUID thread = start_waiter(SIMPLE_SET_CLEAR);
    const int notify = notify_and_block(thread);
    const int runs = callback_runs;
    const int set = sceKernelSetEvent(object, EVENT_BIT_A, SET_USER_DATA);
    const int clear = sceKernelClearEvent(object, 0);
    const SceUInt32 pattern_after_clear = pattern_of(SIMPLE_SET_CLEAR);
    const int status = status_of(thread);
    sceKernelSignalSema(gate, 1);
    int again = 0;
    TEST_WAIT_FOR((again = test_in_wait(thread, WAIT_TYPE_EVENT | WAIT_TYPE_CB, object)) || done);
    const int release = sceKernelSetEvent(object, EVENT_BIT_A, RELEASE_USER_DATA);
    test_join_thread(thread);
    const SceUInt32 pattern = pattern_of(SIMPLE_SET_CLEAR);
    sceKernelDeleteSimpleEvent(object);
    sceKernelDeleteSema(gate);

    PRINTF(notify, "%s: the notify", name);
    PRINTF(runs, "%s: runs of the callback before the set", name);
    PRINTF(set, "%s: the set", name);
    PRINTF(clear, "%s: the clear", name);
    PRINTF(pattern_after_clear, "%s: the pattern after the clear", name);
    PRINTF(status, "%s: T's status during the callback", name);
    PRINTF(again, "%s: T waits again after its callback", name);
    PRINTF(release, "%s: the release", name);
    PRINTF(wait_result, "%s: T's wait result", name);
    PRINTF(wait_bits, "%s: the bits T's wait got", name);
    PRINTF((int)wait_user_data, "%s: the user data low word T's wait got", name);
    PRINTF(pattern, "%s: the pattern after", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    cancel_case(SIMPLE_MANUAL, "a manual reset simple event cancel");
    cancel_case(SIMPLE_AUTO, "an auto reset simple event cancel");
    cancel_case(FLAG_SINGLE, "an event flag cancel");
    cancel_case(FLAG_SINGLE_CLEAR, "an event flag cancel with a clear");
    cancel_case(FLAG_MULTI_QUEUED, "an event flag cancel with a queued waiter");
    set_clear_case();

    test_exit();
    return 0;
}
