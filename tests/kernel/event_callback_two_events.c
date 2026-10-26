// One callback registered to two simple events, notified by set calls with notification on either
// event. A check runs the callback once for all the notifications pending, with the count of them
// and the notify ID and argument of the last one. Cases:
// - a set with notification on each event, then a check
// - the callback unregistered from the first event, then a set with notification on each event,
//   then a check
// The model is event_notify_callback, which has one event. Its one callback and set cases cover the
// notify path, so this adds only the second event.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID cb, second;
static volatile int calls, got_notify_id, got_count, got_arg;

static int recording_callback(int notify_id, int count, int arg, void *common) {
    (void)common;
    calls++;
    got_notify_id = notify_id;
    got_count = count;
    got_arg = arg;
    return 0;
}

// Prints the callback's pending notifications, then runs it and prints what it got
static void print_check(const char *name) {
    calls = 0;
    got_notify_id = got_count = got_arg = 0xDEAD;
    PRINTF(sceKernelGetCallbackCount(cb), "%s: the callback's notifications", name);
    sceKernelCheckCallback();
    PRINTF(calls, "%s: runs of the callback", name);
    PRINTF(got_notify_id == second, "%s: the notify ID is the second event", name);
    PRINTF(got_count, "%s: the count", name);
    PRINTF(got_arg, "%s: the argument", name);
}

int main(void) {
    cb = sceKernelCreateCallback("record", 0, recording_callback, NULL);
    const SceUID first = sceKernelCreateSimpleEvent("first", EVENT_ATTR_NOTIFY_CB_ALL, 0, NULL);
    second = sceKernelCreateSimpleEvent("second", EVENT_ATTR_NOTIFY_CB_ALL, 0, NULL);
    PRINT("register to the first event", sceKernelRegisterCallbackToEvent(first, cb));
    PRINT("register to the second event", sceKernelRegisterCallbackToEvent(second, cb));

    PRINT("set the first with argument 1", sceKernelSetEventWithNotifyCallback(first, EVENT_BIT_A, 0, 1));
    PRINT("set the second with argument 2", sceKernelSetEventWithNotifyCallback(second, EVENT_BIT_B, 0, 2));
    print_check("both registered");

    PRINT("unregister from the first event", sceKernelUnregisterCallbackFromEvent(first, cb));
    PRINT("set the first with argument 3", sceKernelSetEventWithNotifyCallback(first, EVENT_BIT_C, 0, 3));
    PRINT("set the second with argument 4", sceKernelSetEventWithNotifyCallback(second, EVENT_BIT_D, 0, 4));
    print_check("first unregistered");

    sceKernelDeleteSimpleEvent(first);
    sceKernelDeleteSimpleEvent(second);
    sceKernelDeleteCallback(cb);

    test_exit();
    return 0;
}
