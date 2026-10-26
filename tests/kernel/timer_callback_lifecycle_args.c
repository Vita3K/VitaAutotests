// Callbacks registered to an openable timer, and the argument and notify ID each one gets on an
// open, a timer event, a close and a delete. The callback is registered through the creating UID,
// then the timer is opened by name, and each notification is checked with the callback's own count
// and run, as in the simple event model. The cases:
// - the open by name, then a check: one notification with argument 0x100.
// - a one shot 50 ms event set and waited for through the opened UID, then a check: one
//   notification with argument 0, from the event.
// - the opened UID closed, then a check: argument 0x200.
// - the timer stopped and its creating UID deleted, then a check: argument 8, and the callback
//   still runs after the delete.
// - a second timer started with a repeating event of 1 s and deleted while started, then a check:
//   argument 8 again, and no further notification in the 1.5 s after the delete.
// Each check prints the callback's notifications, its runs, the argument it got, and whether its
// notify ID is the registered UID. The argument is printed as 0xDEAD when it didn't run.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define ONE_SHOT_US 50000
#define REPEAT_US 1000000
#define QUIET_US 1500000
#define WAIT_US 2000000

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
    const SceUID timer =
        sceKernelCreateTimer("Lifecycle timer", SCE_KERNEL_ATTR_OPENABLE, NULL);
    PRINT("create the callback", test_created(cb));
    PRINT("create the timer", test_created(timer));
    PRINT("register the callback through the creating UID",
          sceKernelRegisterCallbackToEvent(timer, cb));

    const SceUID opened = sceKernelOpenTimer("Lifecycle timer");
    PRINT("open the timer by name", test_created(opened));
    check("after the open", cb, timer);

    SceUInt64 interval = ONE_SHOT_US;
    PRINT("set a one shot 50 ms event through the opened UID",
          sceKernelSetTimerEvent(opened, TIMER_TYPE_SET_EVENT, &interval, 0));
    PRINT("start the timer through the opened UID", sceKernelStartTimer(opened));
    SceUInt32 result = 0;
    SceUInt64 user_data = 0;
    SceUInt32 timeout = WAIT_US;
    PRINT("wait for the event through the opened UID",
          sceKernelWaitEvent(opened, EVENT_TIMER, &result, &user_data, &timeout));
    TEST_WAIT_FOR(sceKernelGetCallbackCount(cb) > 0);
    check("after the event", cb, timer);

    PRINT("close the opened UID", sceKernelCloseTimer(opened));
    check("after the close", cb, timer);

    PRINT("stop the timer", sceKernelStopTimer(timer));
    PRINTF(sceKernelGetCallbackCount(cb), "after the stop: the callback's notifications");
    PRINT("delete the creating UID", sceKernelDeleteTimer(timer));
    check("after the delete", cb, timer);

    const SceUID armed = sceKernelCreateTimer("Armed repeat", 0, NULL);
    PRINT("register the callback to the second timer",
          sceKernelRegisterCallbackToEvent(armed, cb));
    interval = REPEAT_US;
    PRINT("set a repeating 1 s event",
          sceKernelSetTimerEvent(armed, TIMER_TYPE_SET_EVENT, &interval, 1));
    PRINT("start the second timer", sceKernelStartTimer(armed));
    PRINT("delete the started timer", sceKernelDeleteTimer(armed));
    check("after deleting the started timer", cb, armed);
    sceKernelDelayThread(QUIET_US);
    PRINTF(sceKernelGetCallbackCount(cb), "notifications 1.5 s after the delete");

    PRINT("delete the callback", sceKernelDeleteCallback(cb));
    test_exit();
    return 0;
}
