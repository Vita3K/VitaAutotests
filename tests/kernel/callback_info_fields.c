// Every field of a callback's info after each kind of notify, run and cancel: the notify ID, count,
// argument and attributes, and the function, common pointer and owner thread after a run. All of it
// is on one thread with no waits, so each read is of the state the last call left. The cases:
// - a fresh callback
// - after one notify, then after a second one, with the argument of each
// - inside its run, and after the run
// - after a notify and a cancel
// - after a set with notification on a registered simple event, then after a direct notify, then
//   after a direct notify and a set with notification, to see which writer's notify ID is left
// - with the arguments 0, -1 and 0x80000000 on fresh callbacks, after one notify each
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID self, cb, event;
static int common_marker;
static volatile int in_run_result, in_run_notify_id, in_run_count, in_run_arg, in_run_attr;

static int recording_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    CallbackInfo info = { .size = sizeof(info) };
    in_run_result = sceKernelGetCallbackInfo(cb, (SceKernelCallbackInfo *)&info);
    in_run_notify_id = info.notifyId;
    in_run_count = info.notifyCount;
    in_run_arg = info.notifyArg;
    in_run_attr = info.attr;
    return 0;
}

// Prints the info of id: the notify ID against -1, 0 and the event, then the count, argument and
// attributes as numbers
static void print_info(SceUID id, const char *name) {
    CallbackInfo info = { .size = sizeof(info) };
    PRINTF(sceKernelGetCallbackInfo(id, (SceKernelCallbackInfo *)&info), "%s: the info call", name);
    PRINTF(info.notifyId == -1, "%s: the notify ID is -1", name);
    PRINTF(info.notifyId == 0, "%s: the notify ID is 0", name);
    PRINTF(info.notifyId == event, "%s: the notify ID is the event", name);
    PRINTF(info.notifyCount, "%s: the count", name);
    PRINTF(info.notifyArg, "%s: the argument", name);
    PRINTF(info.attr, "%s: the attributes", name);
}

// Prints what the callback saw in its info during its last run
static void print_recorded(const char *name) {
    PRINTF(in_run_result, "%s: the info call inside", name);
    PRINTF(in_run_notify_id == -1, "%s: the notify ID inside is -1", name);
    PRINTF(in_run_notify_id == 0, "%s: the notify ID inside is 0", name);
    PRINTF(in_run_notify_id == event, "%s: the notify ID inside is the event", name);
    PRINTF(in_run_count, "%s: the count inside", name);
    PRINTF(in_run_arg, "%s: the argument inside", name);
    PRINTF(in_run_attr, "%s: the attributes inside", name);
}

// Prints the function, common pointer and owner thread of cb
static void print_owner(const char *name) {
    CallbackInfo info = { .size = sizeof(info) };
    sceKernelGetCallbackInfo(cb, (SceKernelCallbackInfo *)&info);
    PRINTF(info.callback == recording_callback, "%s: the function", name);
    PRINTF(info.common == &common_marker, "%s: the common pointer", name);
    PRINTF(info.threadId == self, "%s: the owner thread", name);
}

int main(void) {
    self = sceKernelGetThreadId();
    cb = sceKernelCreateCallback("info", 0, recording_callback, &common_marker);
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_NOTIFY_CB_ALL, 0, NULL);
    print_info(cb, "fresh");

    sceKernelNotifyCallback(cb, 0x11);
    print_info(cb, "one notify");
    sceKernelNotifyCallback(cb, 0x12);
    print_info(cb, "two notifies");

    sceKernelCheckCallback();
    print_recorded("run");
    print_info(cb, "after the run");
    print_owner("after the run");

    sceKernelNotifyCallback(cb, 0x21);
    print_info(cb, "one notify before a cancel");
    PRINT("cancel", sceKernelCancelCallback(cb));
    print_info(cb, "after a cancel");

    // Notified through a simple event that has the callback registered to it
    PRINT("register to the event", sceKernelRegisterCallbackToEvent(event, cb));
    PRINT("set the event with notification",
          sceKernelSetEventWithNotifyCallback(event, EVENT_BIT_A, 0, 0x31));
    print_info(cb, "after a set with notification");
    sceKernelNotifyCallback(cb, 0x32);
    print_info(cb, "after a direct notify");
    PRINT("check after the event and the direct notify", sceKernelCheckCallback());

    // The same two writers in the other order, so the one that writes last gives the notify ID
    sceKernelNotifyCallback(cb, 0x41);
    PRINT("set the event with notification after a direct notify",
          sceKernelSetEventWithNotifyCallback(event, EVENT_BIT_B, 0, 0x42));
    print_info(cb, "after a direct notify, then a set with notification");
    PRINT("check after the direct notify and the event", sceKernelCheckCallback());
    PRINT("unregister from the event", sceKernelUnregisterCallbackFromEvent(event, cb));

    // Fresh callbacks with each argument, notified once and not run
    const int args[] = { 0, -1, (int)0x80000000 };
    const char *const arg_names[] = { "argument 0", "argument -1", "argument 0x80000000" };
    for (unsigned int i = 0; i < sizeof(args) / sizeof(args[0]); i++) {
        const SceUID fresh = sceKernelCreateCallback("arg", 0, recording_callback, &common_marker);
        sceKernelNotifyCallback(fresh, args[i]);
        print_info(fresh, arg_names[i]);
        sceKernelCancelCallback(fresh);
        sceKernelDeleteCallback(fresh);
    }

    sceKernelDeleteSimpleEvent(event);
    sceKernelDeleteCallback(cb);
    test_exit();
    return 0;
}
