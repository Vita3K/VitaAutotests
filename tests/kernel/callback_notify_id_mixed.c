// The first argument a callback gets when it has both a direct notify and a notify through a
// simple event pending before one run, in both orders. The callback is registered to the event,
// and the event is set with notification. Before the run the test reads the notify ID in the
// callback's info and its count. Then it runs the callback with sceKernelCheckCallback and prints
// those, how often it ran, whether its first argument was -1 or the event, and the count and
// argument it got. All of it is on one thread with no waits, and each case has a new callback and
// event.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_RUN 0xDEAD

static volatile int runs, got_notify_id, got_count, got_arg;

static int recording_callback(int notify_id, int count, int arg, void *common) {
    (void)common;
    runs++;
    got_notify_id = notify_id;
    got_count = count;
    got_arg = arg;
    return 0;
}

static void run_case(const char *name, int event_first) {
    runs = 0;
    got_notify_id = got_count = got_arg = NOT_RUN;
    const SceUID cb = sceKernelCreateCallback("mixed", 0, recording_callback, NULL);
    const SceUID event = sceKernelCreateSimpleEvent("mixed", EVENT_ATTR_NOTIFY_CB_ALL, 0, NULL);
    PRINTF(sceKernelRegisterCallbackToEvent(event, cb), "%s: register", name);
    if (event_first) {
        PRINTF(sceKernelSetEventWithNotifyCallback(event, EVENT_BIT_A, 0, 0x11), "%s: set with notification", name);
        PRINTF(sceKernelNotifyCallback(cb, 0x22), "%s: notify", name);
    } else {
        PRINTF(sceKernelNotifyCallback(cb, 0x22), "%s: notify", name);
        PRINTF(sceKernelSetEventWithNotifyCallback(event, EVENT_BIT_A, 0, 0x11), "%s: set with notification", name);
    }
    CallbackInfo info = { .size = sizeof(info) };
    sceKernelGetCallbackInfo(cb, (SceKernelCallbackInfo *)&info);
    const int info_notify_id = info.notifyId;
    const int count = sceKernelGetCallbackCount(cb);
    const int check = sceKernelCheckCallback();
    PRINTF(info_notify_id == -1, "%s: the info's notify ID is -1", name);
    PRINTF(count, "%s: the callback's notifications", name);
    PRINTF(check, "%s: the check", name);
    PRINTF(runs, "%s: runs of the callback", name);
    PRINTF(got_notify_id == -1, "%s: its first argument is -1", name);
    PRINTF(got_notify_id == event, "%s: its first argument is the event", name);
    PRINTF(got_count, "%s: the count it got", name);
    PRINTF(got_arg, "%s: the argument it got", name);
    sceKernelUnregisterCallbackFromEvent(event, cb);
    sceKernelDeleteSimpleEvent(event);
    sceKernelDeleteCallback(cb);
}

int main(void) {
    run_case("event, then direct", 1);
    run_case("direct, then event", 0);
    test_exit();
    return 0;
}
