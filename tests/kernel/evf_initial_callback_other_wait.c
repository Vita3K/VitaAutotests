// Another thread's wait for a bit of an event flag, while a thread holds the flag in a callback
// that was pending before its wait with callbacks. The flag has no SCE_EVENT_WAITMULTIPLE and
// starts with bit 0x2 set. T waits with callbacks for bit 0x1, and its callback blocks on a gate,
// so it runs at the start of T's wait. The main thread waits, not polls, for bit 0x2 with a 2 s
// timeout while the callback still blocks. Then the gate opens, the main thread sets bit 0x1 once T
// waits for it, and joins T. The model event_flag_hold_callbacks covers a callback notified during
// the wait. The cases:
// - the main thread's wait for 0x2: its result, and whether it returned within 1 s.
// - the callback's runs.
// - T's wait result.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_TIMEOUT_US (2 * TEST_WAIT_US)
#define MAIN_WAIT_US 2000000
#define WITHIN_US 1000000

static SceUID flag, gate, go, callback;
static volatile int callback_ready, callback_entered, callback_runs, t_wait_result;

// Blocks on the gate, so the main thread can wait while the callback runs
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

// Creates its callback, then waits for go before it calls the wait with callbacks, so the
// callback is pending at the call
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
    t_wait_result = 0xDEAD;

    const SceUID t = test_start_thread(t_entry);
    TEST_WAIT_FOR(callback_ready);
    sceKernelNotifyCallback(callback, 1);
    sceKernelSignalSema(go, 1);
    TEST_WAIT_FOR(callback_entered);

    // The main thread waits for bit 0x2 while the callback holds the flag, if it does
    SceUInt32 bits = 0;
    SceUInt32 timeout = MAIN_WAIT_US;
    const SceUInt64 begin = sceKernelGetProcessTimeWide();
    const int main_wait = sceKernelWaitEventFlag(flag, 0x2, SCE_EVENT_WAITOR, &bits, &timeout);
    const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - begin;
    PRINT("the main thread's wait for 0x2", main_wait);
    PRINT("it returned within 1 s", elapsed < WITHIN_US);
    sceKernelSignalSema(gate, 1);

    // Once T waits for bit 0x1, setting it ends the wait
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_EVENT_FLAG | WAIT_TYPE_CB, flag));
    sceKernelSetEventFlag(flag, 0x1);
    test_join_thread(t);

    PRINT("runs of the callback", callback_runs);
    PRINT("T's wait", t_wait_result);

    sceKernelDeleteSema(gate);
    sceKernelDeleteSema(go);
    sceKernelDeleteEventFlag(flag);
    test_exit();
    return 0;
}
