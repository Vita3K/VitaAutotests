// What a wait with callbacks writes to its output pointers when its object is deleted by the
// callback that ended its wait. Thread T waits with callbacks on an object, with its output
// pointers filled with sentinel values. A notification ends T's wait, T runs its callback and the
// callback deletes the waited object. Then the wait returns, and the main thread prints the
// result and what the wait left in each output once T ended. Every case has fresh objects and a
// fresh T. The cases:
// - a simple event waited for a bit, ended by sceKernelNotifyCallback
// - a simple event waited for a bit, with T's callback registered to the event, ended by a set of
//   another bit with notification (the callback unregisters itself before it deletes the event)
// - an event flag waited for a bit, ended by sceKernelNotifyCallback
// - a timer event that never fires, ended by sceKernelNotifyCallback
// Each case prints the delete's result in the callback, how often the callback ran, the wait's
// result, the result pattern and the user data (low and high word) of the wait, where the
// outputs of a flag only have the bits.
//
// On a PS Vita TV on firmware 3.65 the delete succeeds in the callback, the callback runs once and
// every wait returns 0x80028006. The simple event and timer event waits write result pattern 0x10
// and user data 0, the values the event's end gives its other waiters, and the event flag wait
// writes bits 0. None of the sentinels stays.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define SENTINEL_BITS 0xA5A5A5A5
#define SENTINEL_DATA 0x123456789ABCDEF0ull

typedef enum Kind { EVENT_NOTIFY, EVENT_SET_NOTIFY, FLAG, TIMER } Kind;

static Kind kind;
static SceUID object, cb;
static volatile int callback_runs, delete_result, t_result;
static volatile SceUInt32 out_bits;
static volatile SceUInt64 out_data;

// Deletes the object of the case, once
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    if (callback_runs++)
        return 0;
    switch (kind) {
    case EVENT_SET_NOTIFY:
        sceKernelUnregisterCallbackFromEvent(object, cb);
        delete_result = sceKernelDeleteSimpleEvent(object);
        break;
    case EVENT_NOTIFY: delete_result = sceKernelDeleteSimpleEvent(object); break;
    case FLAG: delete_result = sceKernelDeleteEventFlag(object); break;
    case TIMER: delete_result = sceKernelDeleteTimer(object); break;
    }
    return 0;
}

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    SceUInt32 bits = SENTINEL_BITS;
    SceUInt64 user_data = SENTINEL_DATA;
    cb = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    switch (kind) {
    case EVENT_NOTIFY:
    case EVENT_SET_NOTIFY: t_result = sceKernelWaitEventCB(object, EVENT_BIT_A, &bits, &user_data, &timeout); break;
    case FLAG: t_result = sceKernelWaitEventFlagCB(object, 0x1, SCE_EVENT_WAITOR, &bits, &timeout); break;
    case TIMER: t_result = sceKernelWaitEventCB(object, EVENT_TIMER, &bits, &user_data, &timeout); break;
    }
    out_bits = bits;
    out_data = user_data;
    sceKernelDeleteCallback(cb);
    return 0;
}

static void run_case(const char *name, Kind k) {
    kind = k;
    callback_runs = 0;
    delete_result = t_result = NOT_DONE;
    switch (k) {
    case EVENT_NOTIFY:
    case EVENT_SET_NOTIFY: object = sceKernelCreateSimpleEvent("event", 0, 0, NULL); break;
    case FLAG: object = sceKernelCreateEventFlag("flag", 0, 0, NULL); break;
    case TIMER: object = sceKernelCreateTimer("timer", 0, NULL); break;
    }
    const SceUID t = test_start_thread(t_entry);
    test_wait_until_waiting(t);
    if (k == EVENT_SET_NOTIFY) {
        sceKernelRegisterCallbackToEvent(object, cb);
        sceKernelSetEventWithNotifyCallback(object, EVENT_BIT_B, 0, 0x55);
    } else {
        sceKernelNotifyCallback(cb, 1);
    }
    test_join_thread(t);

    PRINTF(delete_result, "%s: the delete in the callback", name);
    PRINTF(callback_runs, "%s: runs of the callback", name);
    PRINTF(t_result, "%s: the wait", name);
    PRINTF(out_bits, "%s: the result pattern", name);
    if (k != FLAG) {
        PRINTF((SceUInt32)out_data, "%s: the user data, low word", name);
        PRINTF((SceUInt32)(out_data >> 32), "%s: the user data, high word", name);
    }
}

int main(void) {
    run_case("simple event, notify callback", EVENT_NOTIFY);
    run_case("simple event, set with notification", EVENT_SET_NOTIFY);
    run_case("event flag, notify callback", FLAG);
    run_case("timer event, notify callback", TIMER);

    test_exit();
    return 0;
}
