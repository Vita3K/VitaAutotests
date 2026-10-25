// When the callbacks of a sceKernelWaitMultipleEventsCB call run. A thread T creates a callback C and
// notifies it, which only leaves it pending since T isn't waiting, then calls the wait with an OR on
// one simple event E. C runs at the start of the wait. On its first run it notifies itself again,
// so it is pending once more, and on its second run it sets E, which would end a wait that is still
// going. Right after the call T reads the call's result, C's runs, the time left and whether E is
// set, then calls sceKernelCheckCallback and reads C's runs again. Every case has a fresh E and C.
// The cases:
// - E isn't set and the timeout is 200 ms, so the call sleeps. It also prints whether it lasted at
//   least 100 ms.
// - E is set before the call, with a timeout of 1 s, so the call ends without sleeping
// - E isn't set and the timeout is 0, so the deadline has passed when the call would sleep
// The time left prints 0 for none, 1 for the timeout passed and 2 for something else.
//
// On a PS Vita TV on firmware 3.65 the call that sleeps isn't woken by the callback pending since
// its start: it times out with 0x80028005 after the 200 ms, and C's second run happens before the
// call returns, which leaves E set but keeps the timeout. The call with E set returns 1 after C's
// first run, with the time left as passed, and C's second run waits for the check. The call with a
// timeout of 0 returns 0x80028005 after C's first run, with E not set, and C's second run waits for
// the check.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define SLEEP_US 200000
#define SET_US 1000000
#define LASTED_US 100000

static SceUID event, cb;
static volatile int runs, call_result, runs_at_return, event_set, lasted, runs_after_check;
static SceUInt32 timeout, passed;

// Notifies itself on its first run and sets the event on its second
static int callback(SceUID notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    if (++runs == 1)
        sceKernelNotifyCallback(cb, 1);
    else
        sceKernelSetEvent(event, EVENT_BIT_A, 0);
    return 0;
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    cb = sceKernelCreateCallback("final check", 0, callback, NULL);
    sceKernelNotifyCallback(cb, 1);
    WaitEvent list[1] = { { event, EVENT_BIT_A } };
    ResultEvent results[1];
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    call_result = sceKernelWaitMultipleEventsCB(list, 1, EVENT_WAIT_MODE_OR, results, &timeout);
    lasted = sceKernelGetProcessTimeWide() - start >= LASTED_US;
    runs_at_return = runs;
    SceUInt32 bits = 0;
    event_set = sceKernelPollEvent(event, EVENT_BIT_A, &bits, NULL) == 0;
    sceKernelCheckCallback();
    runs_after_check = runs;
    sceKernelDeleteCallback(cb);
    return 0;
}

static void run(const char *name, int set_before, SceUInt32 timeout_us) {
    event = sceKernelCreateSimpleEvent("final check", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    if (set_before)
        sceKernelSetEvent(event, EVENT_BIT_A, 0);
    runs = 0;
    timeout = passed = timeout_us;
    test_join_thread(test_start_thread(waiter));
    PRINTF(call_result, "%s: the call", name);
    PRINTF(runs_at_return, "%s: runs of the callback when it returned", name);
    PRINTF(timeout == 0 ? 0 : timeout == passed ? 1 : 2, "%s: the time left", name);
    PRINTF(event_set, "%s: the event is set when it returned", name);
    PRINTF(runs_after_check, "%s: runs of the callback after a check", name);
    sceKernelDeleteSimpleEvent(event);
}

int main(void) {
    run("a call that sleeps", 0, SLEEP_US);
    PRINT("a call that sleeps: it lasted at least 100 ms", lasted);
    run("a call with the event set", 1, SET_US);
    run("a call with a timeout of 0", 0, 0);
    test_exit();
    return 0;
}
