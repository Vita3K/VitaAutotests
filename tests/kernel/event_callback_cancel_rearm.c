// A callback registered to a simple event, notified by two set calls with notification, then
// canceled, then notified again by one more set with notification. The cancel drops the two
// notifications, and the new one runs the callback once with its own count and argument. Cases:
// - two set calls with notification, the count, sceKernelCancelCallback, the count, one more set
//   call with notification, the count, then a check
// The model is callback, which covers the cancel of notifications made by sceKernelNotifyCallback.
// This covers the same cancel for notifications made by the event's set calls.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static volatile int calls, got_count, got_arg;

static int recording_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)common;
    calls++;
    got_count = count;
    got_arg = arg;
    return 0;
}

int main(void) {
    const SceUID cb = sceKernelCreateCallback("record", 0, recording_callback, NULL);
    const SceUID event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_NOTIFY_CB_ALL, 0, NULL);
    PRINT("register", sceKernelRegisterCallbackToEvent(event, cb));
    PRINT("set with argument 1", sceKernelSetEventWithNotifyCallback(event, EVENT_BIT_A, 0, 1));
    PRINT("set with argument 2", sceKernelSetEventWithNotifyCallback(event, EVENT_BIT_B, 0, 2));
    PRINT("the callback's notifications before the cancel", sceKernelGetCallbackCount(cb));
    PRINT("cancel", sceKernelCancelCallback(cb));
    PRINT("the callback's notifications after the cancel", sceKernelGetCallbackCount(cb));
    PRINT("set with argument 3", sceKernelSetEventWithNotifyCallback(event, EVENT_BIT_C, 0, 3));
    PRINT("the callback's notifications after the new set", sceKernelGetCallbackCount(cb));
    sceKernelCheckCallback();
    PRINT("runs of the callback", calls);
    PRINT("the count the callback got", got_count);
    PRINT("the argument the callback got", got_arg);

    sceKernelDeleteSimpleEvent(event);
    sceKernelDeleteCallback(cb);

    test_exit();
    return 0;
}
