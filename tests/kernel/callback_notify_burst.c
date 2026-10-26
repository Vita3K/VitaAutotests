// A callback registered to a simple event, notified 30 times by set calls with notification and 20
// times by sceKernelNotifyCallback, before any check. A check runs the callback once for all the
// notifications pending, and a second check runs it nothing more. Cases:
// - 30 set calls with notification, arguments 0 to 29, then 20 notify calls, arguments 0 to 19
// - a check, then a second check with nothing new pending
// The model is callback, which notifies the callback directly. This adds the notifications of the
// event's set calls, and a burst of them.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define SET_COUNT 30
#define NOTIFY_COUNT 20

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

    int set_ok = 0;
    for (int i = 0; i < SET_COUNT; i++)
        set_ok += sceKernelSetEventWithNotifyCallback(event, EVENT_BIT_A, 0, i) == 0;
    PRINT("set calls with notification that returned 0", set_ok);

    int notify_ok = 0;
    for (int i = 0; i < NOTIFY_COUNT; i++)
        notify_ok += sceKernelNotifyCallback(cb, i) == 0;
    PRINT("notify calls that returned 0", notify_ok);

    PRINT("the callback's notifications before the check", sceKernelGetCallbackCount(cb));
    sceKernelCheckCallback();
    PRINT("runs after the first check", calls);
    PRINT("the count the callback saw", got_count);
    PRINT("the last argument the callback saw", got_arg);
    sceKernelCheckCallback();
    PRINT("runs after the second check", calls);

    sceKernelDeleteSimpleEvent(event);
    sceKernelDeleteCallback(cb);

    test_exit();
    return 0;
}
