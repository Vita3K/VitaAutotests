// A callback notified after the vblank woke the waiter and before the waiter ran again. Whether
// sceDisplayWaitVblankStartCB runs the callback before it returns, and how many vblanks it takes.
// The waiter A creates its callback and calls sceDisplayWaitVblankStartCB once. It reads the vblank
// count right after the wait, and records whether the callback ran by the time the
// wait returned and the wait's result. Then it calls sceKernelCheckCallback and records its result
// and whether the callback ran. The callback records its count and argument at its first run. A runs
// at a low priority, the main thread at a higher one, both on core 0. Each attempt has a fresh A
// and callback. The cases:
// - notify after the wake: the main thread waits until A is in the wait, then calls
//   sceDisplayWaitVblankStart. The same vblank wakes both. The main thread runs first, reads the
//   vblank count, reads A's thread info and notifies the callback, then waits for A to end. An
//   attempt counts only if A's status at the notify is ready. Attempts repeat, up to a limit, until
//   both outcomes were seen at least once: the callback ran inside the wait, and the callback was
//   still pending when the wait returned. The first attempt of each outcome is printed, under its
//   own label. An outcome that was never seen prints only that it was not caught.
// - notify during the wait: the reference case. The main thread reads the vblank count and
//   notifies the callback once A is in the wait, before the vblank. Attempts repeat until A's
//   status at the notify is waiting. The last attempt is printed.
// For each printed attempt the vblank count at the end of A's wait is compared with the count the
// main thread read at the notify. Only the relations are printed, never the counts.
//
// On a PS Vita TV on firmware 3.65 both outcomes of a notify after the wake happen. In one the
// callback runs before the wait returns, in the other the wait returns with it pending and the check
// runs it. In both the wait returns in the vblank of the notify, so the callback costs no extra
// vblank. A notify during the wait runs the callback inside the wait, which returns one vblank
// after the notify.
#include "vblank_cb.h"

#define THREAD_STATUS_READY 0x2
#define MAX_TRIES 300

static SceUID callback;
static volatile int ready, done;
static volatile int runs, first_count, first_arg;
static volatile int runs_in_wait, wait_result, check_result, runs_after_check, v_after;

typedef struct Result {
    int caught, status, wait_type, notify, wait_result, runs_in_wait, check_result, runs_after_check;
    int first_count, first_arg, wait_delta;
} Result;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)common;
    if (runs == 0) {
        first_count = count;
        first_arg = arg;
    }
    runs++;
    return 0;
}

static int a_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    ready = 1;
    wait_result = sceDisplayWaitVblankStartCB();
    v_after = sceDisplayGetVcount();
    runs_in_wait = runs;
    check_result = sceKernelCheckCallback();
    runs_after_check = runs;
    sceKernelDeleteCallback(callback);
    done = 1;
    return 0;
}

// Runs one attempt and fills r
static void attempt(int after_wake, Result *r) {
    ready = done = runs = 0;
    first_count = first_arg = NOT_DONE;
    runs_in_wait = wait_result = check_result = runs_after_check = v_after = NOT_DONE;
    const SceUID a = start_w(a_entry);
    TEST_WAIT_FOR(ready && in_cb_wait(a));
    if (after_wake)
        sceDisplayWaitVblankStart();
    const int v_notify = sceDisplayGetVcount();
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(a, &info);
    r->status = info.status;
    r->wait_type = info.waitType;
    r->notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(done);
    test_join_thread(a);
    r->wait_result = wait_result;
    r->runs_in_wait = runs_in_wait;
    r->check_result = check_result;
    r->runs_after_check = runs_after_check;
    r->first_count = first_count;
    r->first_arg = first_arg;
    r->wait_delta = (v_after - v_notify) & 0xFFFF;
}

static void print_result(const char *name, const Result *r) {
    PRINTF(r->caught, "%s: caught", name);
    if (!r->caught)
        return;
    PRINTF(r->status, "%s: A's status at the notify", name);
    PRINTF(r->wait_type, "%s: A's wait type at the notify", name);
    PRINTF(r->notify, "%s: the notify", name);
    PRINTF(r->wait_result, "%s: the wait's result", name);
    PRINTF(r->runs_in_wait, "%s: runs of the callback when the wait returned", name);
    PRINTF(r->check_result, "%s: the check's result", name);
    PRINTF(r->runs_after_check, "%s: runs of the callback after the check", name);
    PRINTF(r->first_count, "%s: the count the callback got at its first run", name);
    PRINTF(r->first_arg, "%s: the argument the callback got at its first run", name);
    PRINTF(r->wait_delta == 0, "%s: the wait returned in the vblank of the notify", name);
    PRINTF(r->wait_delta == 1, "%s: the wait returned one vblank after the notify", name);
    PRINTF(r->wait_delta > 1 && r->wait_delta < 0x8000, "%s: the wait returned more than one vblank after the notify", name);
}

static void run_after_wake(void) {
    Result ran = {}, pending = {};
    for (int tries = 0; tries < MAX_TRIES && !(ran.caught && pending.caught); tries++) {
        Result r = {};
        attempt(1, &r);
        if (r.status != THREAD_STATUS_READY)
            continue;
        if (r.runs_in_wait > 0 && r.runs_in_wait != NOT_DONE) {
            if (!ran.caught) {
                ran = r;
                ran.caught = 1;
            }
        } else if (!pending.caught) {
            pending = r;
            pending.caught = 1;
        }
    }
    print_result("after the wake, callback ran in the wait", &ran);
    print_result("after the wake, callback left pending", &pending);
}

static void run_during_wait(void) {
    Result r = {};
    for (int tries = 0; tries < MAX_TRIES && !r.caught; tries++) {
        attempt(0, &r);
        r.caught = r.status == THREAD_STATUS_WAITING;
    }
    print_result("notify during the wait", &r);
}

int main(void) {
    pin_main();
    run_after_wake();
    run_during_wait();
    test_exit();
    return 0;
}
