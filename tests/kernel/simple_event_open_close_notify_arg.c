// Callback notifications through an openable simple event, with the callback registered through the
// creating UID and the event opened by name. The model opened_uid_reports covers a callback
// registered through the opened UID, and the notifications the other way round. Here the event is
// opened after the registration, a set with notification goes through the opened UID, the opened
// UID is closed, and a set through the creating UID notifies again. The cases:
// - the open by name alone, then a check: whether the open notifies the callback, and with what
//   argument.
// - a set with notification through the opened UID with argument 0x11.
// - the opened UID closed, then a check with nothing else notified: whether the close notifies it.
// - a set with notification through the creating UID with argument 0x22, after the close.
// Each check prints the callback's notifications, its runs, the argument it got, and whether its
// notify ID is the registered UID. The argument is printed as 0xDEAD when it didn't run.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static volatile int calls, got_arg, got_notify_id;

static int recording_callback(int notify_id, int count, int arg, void *common) {
    (void)count;
    (void)common;
    calls++;
    got_arg = arg;
    got_notify_id = notify_id;
    return 0;
}

// Prints the callback's notifications, runs it, and prints what it got
static void check(const char *name, SceUID cb, SceUID registered) {
    calls = 0;
    got_arg = 0xDEAD;
    got_notify_id = 0xDEAD;
    PRINTF(sceKernelGetCallbackCount(cb), "%s: the callback's notifications", name);
    sceKernelCheckCallback();
    PRINTF(calls, "%s: runs of the callback", name);
    PRINTF(got_arg, "%s: the argument it got", name);
    if (calls)
        PRINTF(got_notify_id == registered, "%s: its notify ID is the registered UID", name);
}

int main(void) {
    const SceUID cb = sceKernelCreateCallback("record", 0, recording_callback, NULL);
    const SceUID event =
        sceKernelCreateSimpleEvent("Opened notify", SCE_KERNEL_ATTR_OPENABLE, 0, NULL);
    PRINT("create the callback", test_created(cb));
    PRINT("create the event", test_created(event));
    PRINT("register the callback through the creating UID",
          sceKernelRegisterCallbackToEvent(event, cb));

    const SceUID opened = sceKernelOpenSimpleEvent("Opened notify");
    PRINT("open the event by name", test_created(opened));
    check("after the open", cb, event);
    PRINT("set with notification through the opened UID",
          sceKernelSetEventWithNotifyCallback(opened, EVENT_BIT_A, 0, 0x11));
    check("notified through the opened UID", cb, event);

    PRINT("close the opened UID", sceKernelCloseSimpleEvent(opened));
    check("after the close", cb, event);
    PRINT("set with notification through the creating UID after the close",
          sceKernelSetEventWithNotifyCallback(event, EVENT_BIT_B, 0, 0x22));
    check("notified through the creating UID after the close", cb, event);

    PRINT("unregister the callback", sceKernelUnregisterCallbackFromEvent(event, cb));
    PRINT("delete the event", sceKernelDeleteSimpleEvent(event));
    sceKernelDeleteCallback(cb);
    test_exit();
    return 0;
}
