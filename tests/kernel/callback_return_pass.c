// How the return value of a callback changes one pass over a thread's callbacks. Three callbacks A,
// B and C are created in that order and each is notified once with its own argument, so one check
// runs all three in creation order. The middle callback, or the first one, returns a value X when
// it runs and the others return 0. The values are 1, -1, 0x7FFFFFFF, 0x80000000 and 0x100. For each
// case the test prints the check's result, how often each callback ran, and a notify of each
// callback after the check. A notify gives 0 for a callback that is still there and 0x800280A1 for
// one that was deleted. The test checks whether a nonzero return deletes the callback that returned
// it, and whether a negative one also stops the pass, so the callbacks after it wait for the next
// check. The last cases register the callback that returns to a simple event, with a return of 1
// and of -1, and then set that event with notification. The test checks that the set doesn't run
// the deleted callback again. Every case has fresh callbacks and runs on the main thread alone.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CALLBACKS 3

static SceUID callbacks[CALLBACKS];
static volatile int runs[CALLBACKS];
// The slot of the callback that returns returned, the middle one or the first one
static volatile int returner, returned;

// Counts its run in the slot its notify argument names, and returns returned if it is the returner
static int pass_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)common;
    const int slot = arg - 1;
    runs[slot]++;
    return slot == returner ? returned : 0;
}

// Creates the three callbacks and notifies them in creation order, each with its slot number
static void create_and_notify(void) {
    static const char *const names[CALLBACKS] = { "a", "b", "c" };
    for (int i = 0; i < CALLBACKS; i++) {
        runs[i] = 0;
        callbacks[i] = sceKernelCreateCallback(names[i], 0, pass_callback, NULL);
        sceKernelNotifyCallback(callbacks[i], i + 1);
    }
}

static void delete_all(void) {
    for (int i = 0; i < CALLBACKS; i++)
        sceKernelDeleteCallback(callbacks[i]);
}

// Runs one pass of the three callbacks with the given returner and value, and prints it
static void run_pass(const char *who, int value) {
    returned = value;
    create_and_notify();
    const int result = sceKernelCheckCallback();
    PRINTF(result, "%s returns 0x%X: the check", who, (unsigned)value);
    PRINTF(runs[0], "%s returns 0x%X: runs of A", who, (unsigned)value);
    PRINTF(runs[1], "%s returns 0x%X: runs of B", who, (unsigned)value);
    PRINTF(runs[2], "%s returns 0x%X: runs of C", who, (unsigned)value);
    for (int i = 0; i < CALLBACKS; i++)
        PRINTF(sceKernelNotifyCallback(callbacks[i], 1), "%s returns 0x%X: notify %c after", who,
               (unsigned)value, 'A' + i);
    delete_all();
}

// The callback that returns is also registered to a simple event. The direct check runs it, which
// deletes it, and the set with notification afterwards must not run it again.
static void run_registered(int value) {
    returner = 0;
    returned = value;
    runs[0] = 0;
    const SceUID event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_NOTIFY_CB_ALL, 0, NULL);
    callbacks[0] = sceKernelCreateCallback("registered", 0, pass_callback, NULL);
    PRINTF(sceKernelRegisterCallbackToEvent(event, callbacks[0]),
           "registered returns 0x%X: register", (unsigned)value);
    sceKernelNotifyCallback(callbacks[0], 1);
    PRINTF(sceKernelCheckCallback(), "registered returns 0x%X: the check", (unsigned)value);
    PRINTF(runs[0], "registered returns 0x%X: runs before the set", (unsigned)value);
    PRINTF(sceKernelSetEventWithNotifyCallback(event, EVENT_BIT_A, 0, 7),
           "registered returns 0x%X: the set", (unsigned)value);
    PRINTF(sceKernelCheckCallback(), "registered returns 0x%X: the check after the set",
           (unsigned)value);
    PRINTF(runs[0], "registered returns 0x%X: runs after the set", (unsigned)value);
    sceKernelDeleteSimpleEvent(event);
    sceKernelDeleteCallback(callbacks[0]);
}

int main(void) {
    static const int values[] = { 1, -1, 0x7FFFFFFF, (int)0x80000000, 0x100 };
    static const int count = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < count; i++) {
        returner = 1;
        run_pass("the middle callback", values[i]);
    }
    for (int i = 0; i < count; i++) {
        returner = 0;
        run_pass("the first callback", values[i]);
    }
    returner = 0;
    run_registered(1);
    run_registered(-1);

    test_exit();
    return 0;
}
