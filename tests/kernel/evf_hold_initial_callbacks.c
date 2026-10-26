// Whether an event flag without SCE_EVENT_WAITMULTIPLE is held by its waiting
// thread T while T runs a callback that was pending before T called
// sceKernelWaitEventFlagCB. event_flag_hold_callbacks covers a callback
// notified during the wait, which runs while the wait is already counted. Here
// the callback is notified before the call, so it runs at the start of the
// wait. The flag starts with bit 0x2 set. T waits for bit 0x1 with callbacks,
// and its callback blocks on a gate semaphore, so the main thread can poll the
// flag while T is in the callback. A poll for bit 0x2 fails with 0x800280E2 if
// T holds the flag there, and succeeds with 0 if it doesn't. The main thread
// then lets the callback return, sets bit 0x1 once T waits for it, and joins T.
// It prints the poll's result, the callback's runs and T's wait result.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_TIMEOUT_US (2 * TEST_WAIT_US)

static SceUID flag, gate, go, callback;
static volatile int callback_ready, callback_entered, callback_runs,
    t_wait_result;
static volatile int poll_result;

// Blocks on the gate, so the main thread can poll while the callback runs
static int blocking_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_entered = 1;
    SceUInt timeout = GATE_TIMEOUT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    callback_runs++;
    return 0;
}

// Creates its callback, then waits for go before it calls the wait with
// callbacks, so the callback is pending at the call
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("hold", 0, blocking_callback, NULL);
    callback_ready = 1;
    SceUInt timeout = GATE_TIMEOUT_US;
    sceKernelWaitSema(go, 1, &timeout);
    SceUInt32 bits = 0;
    timeout = GATE_TIMEOUT_US;
    t_wait_result = sceKernelWaitEventFlagCB(flag, 0x1, SCE_EVENT_WAITOR, &bits, &timeout);
    sceKernelDeleteCallback(callback);
    return 0;
}

int main(void) {
    flag = sceKernelCreateEventFlag("flag", 0, 0x2, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    callback_ready = callback_entered = callback_runs = 0;
    t_wait_result = poll_result = 0xDEAD;

    const SceUID t = test_start_thread(t_entry);
    TEST_WAIT_FOR(callback_ready);
    sceKernelNotifyCallback(callback, 1);
    sceKernelSignalSema(go, 1);

    // T's callback runs at the start of its wait, and blocks until the gate is
    // signaled
    TEST_WAIT_FOR(callback_entered);
    SceUInt32 bits = 0;
    poll_result = sceKernelPollEventFlag(flag, 0x2, SCE_EVENT_WAITOR, &bits);
    sceKernelSignalSema(gate, 1);

    // Once T waits for bit 0x1, setting it ends the wait
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_EVENT_FLAG | WAIT_TYPE_CB, flag));
    sceKernelSetEventFlag(flag, 0x1);
    test_join_thread(t);

    PRINT("the poll while the callback runs", poll_result);
    PRINT("runs of the callback", callback_runs);
    PRINT("T's wait", t_wait_result);

    sceKernelDeleteSema(gate);
    sceKernelDeleteSema(go);
    sceKernelDeleteEventFlag(flag);
    test_exit();
    return 0;
}
