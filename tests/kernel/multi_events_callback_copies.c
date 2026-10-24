// What a callback that writes the event list or the timeout of a sceKernelWaitMultipleEventsCB call
// changes. A thread T creates a callback C, notifies it, which only leaves it pending since T isn't
// waiting, and calls sceKernelWaitMultipleEventsCB with an OR wait on one simple event, with the
// list and the timeout in variables. C runs at the start of the wait and writes one of them. A
// callback notified while the wait sleeps isn't used, since that hangs the console. Every case has
// fresh events, runs T alone, and prints the call's result and how often C ran. The cases:
// - C rewrites the entry's event UID from an event that isn't set to one that is, with a timeout of
//   0.5 s. It prints whether the call lasted at least 0.25 s, which it does if it still waits on
//   the first event.
// - C writes 12345 to the timeout of 1 s, and the event is set, so the call ends without sleeping.
//   It prints what the variable holds after the call: 0 for the 1000000 passed, 1 for the 12345 C
//   wrote, 2 for something else.
// - C writes 1 to the timeout of 0.5 s, and the event isn't set. It prints whether the call lasted
//   at least 0.25 s, which it does if C's write didn't shorten the timeout, and whether the
//   variable holds 0 after the call.
//
// On a PS Vita TV on firmware 3.65 the call works on copies of the list and the timeout it takes
// before C runs. The rewritten UID changes nothing: the call waits on the first event and times out
// with 0x80028005 after the 0.5 s. With the event set the call returns 1, and the variable holds the
// 1000000 passed again. The shortened timeout changes nothing either: the call times out with
// 0x80028005 after the 0.5 s and the variable holds 0. C runs once in each case.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define WAIT_US 1000000
#define WRITTEN 12345
#define SHORT_WAIT_US 500000
#define LASTED_US 250000

typedef enum Kind { REWRITE_UID, REWRITE_TIMEOUT_SET, SHORTEN_TIMEOUT } Kind;

static Kind kind;
static SceUID unset_event, set_event;
static WaitEvent list[1];
static ResultEvent results[1];
static SceUInt32 timeout;
static volatile int callback_runs, call_result, lasted;

static int callback(SceUID notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    if (kind == REWRITE_UID)
        list[0].eventId = set_event;
    else if (kind == REWRITE_TIMEOUT_SET)
        timeout = WRITTEN;
    else
        timeout = 1;
    return 0;
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    const SceUID cb = sceKernelCreateCallback("copies", 0, callback, NULL);
    sceKernelNotifyCallback(cb, 1);
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    call_result = sceKernelWaitMultipleEventsCB(list, 1, EVENT_WAIT_MODE_OR, results, &timeout);
    lasted = sceKernelGetProcessTimeWide() - start >= LASTED_US;
    sceKernelDeleteCallback(cb);
    return 0;
}

static void run(const char *name, Kind k, SceUID waited, SceUInt32 timeout_us) {
    kind = k;
    callback_runs = 0;
    call_result = lasted = 0xDEAD;
    list[0] = (WaitEvent){ waited, EVENT_BIT_A };
    timeout = timeout_us;
    test_join_thread(test_start_thread(waiter));
    PRINTF(call_result, "%s: the call", name);
    PRINTF(callback_runs, "%s: runs of the callback", name);
}

int main(void) {
    unset_event = sceKernelCreateSimpleEvent("unset", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    set_event = sceKernelCreateSimpleEvent("set", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    sceKernelSetEvent(set_event, EVENT_BIT_A, 0);

    run("the callback rewrites the event UID", REWRITE_UID, unset_event, SHORT_WAIT_US);
    PRINT("the callback rewrites the event UID: it lasted at least 0.25 s", lasted);

    run("the callback writes the timeout, the event is set", REWRITE_TIMEOUT_SET, set_event, WAIT_US);
    PRINT("the callback writes the timeout, the event is set: the timeout after",
        timeout == WAIT_US ? 0 : timeout == WRITTEN ? 1 : 2);

    run("the callback shortens the timeout", SHORTEN_TIMEOUT, unset_event, SHORT_WAIT_US);
    PRINT("the callback shortens the timeout: it lasted at least 0.25 s", lasted);
    PRINT("the callback shortens the timeout: the timeout after is 0", timeout == 0);

    sceKernelDeleteSimpleEvent(unset_event);
    sceKernelDeleteSimpleEvent(set_event);
    test_exit();
    return 0;
}
