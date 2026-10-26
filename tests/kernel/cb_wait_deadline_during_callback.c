// What a wait with callbacks does with its deadline when a callback runs during the wait, and what
// a plain wait does with a notify. The waits are a signal wait, a thread end wait and a delay, each
// with a timeout of 1 second, and every case has a fresh thread, callback, gate and target. Each
// scenario is run for each kind. callback_outer_timeout covers the delay and the semaphore, this
// covers the other two kinds. The scenarios:
// - notified at 500 ms, with callbacks: the callback runs at once and nothing ends the wait, so the
//   wait goes on to its deadline. It prints the result, whether the wait returned between 750 ms
//   and 1250 ms after the call, whether the time left is 0 and how often the callback ran.
// - held, with an event: the callback is notified at once and blocks until the test releases it,
//   1.6 s later. The deadline passes during the callback. At 1.25 s the signal is sent to T, or the
//   target returns with status 0x5A. A delay has no event, so it only waits for the release. It
//   prints the result, whether the time left is above 0, the status of the end and how often the
//   callback ran.
// - held, no event: the same without the event. It prints the result, how often the callback ran,
//   and whether T returned within 250 ms of the release, which it does only if the deadline was
//   kept and not restarted in the callback.
// - plain, notified at 500 ms: the wait has no callbacks, so the notify must not end it. The
//   callback is run by the check that T calls after the wait. It prints whether the wait lasted at
//   least 750 ms, the result, how often the callback ran when the wait returned, the result of
//   sceKernelCheckCallback and how often the callback ran after that check.
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

#define WAIT_US 1000000
#define NOTIFY_EARLY_US 500000
#define EVENT_US 1250000
#define RELEASE_US 1600000
#define NEAR_RELEASE_US 250000
#define MIN_LASTED_US 750000
#define MAX_EARLY_US 1250000
#define LONG_TIMEOUT (3 * TEST_WAIT_US)

typedef enum Scenario {
    NOTIFIED_EARLY,
    HELD_WITH_EVENT,
    HELD,
    PLAIN
} Scenario;

typedef enum Kind {
    SIGNAL,
    THREAD_END,
    DELAY
} Kind;

static Scenario scenario;
static Kind kind;
static SceUID cb, callback_gate, target_gate, target, waiter_thread;
static volatile int callback_runs, hold;
static volatile int result, check, runs_at_return, runs_after_check;
static volatile SceUInt time_left;
static volatile SceUInt64 elapsed, returned_at, release_at;
static int end_status;

// Counts its runs, and blocks until the test releases it when the scenario holds it
static int notified_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    if (hold) {
        SceUInt timeout = LONG_TIMEOUT;
        sceKernelWaitSema(callback_gate, 1, &timeout);
    }
    return 0;
}

// Waits for the target gate, then returns 0x5A, which the end waits get as the status
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = LONG_TIMEOUT;
    sceKernelWaitSema(target_gate, 1, &timeout);
    return 0x5A;
}

// Returns the wait type of the kind, with the callback bit unless the scenario is plain
static SceUInt32 wanted_type(void) {
    SceUInt32 type = WAIT_TYPE_DELAY;
    if (kind == SIGNAL)
        type = WAIT_TYPE_SIGNAL;
    else if (kind == THREAD_END)
        type = WAIT_TYPE_THREAD_END;
    return scenario == PLAIN ? type : type | WAIT_TYPE_CB;
}

// Returns whether T waits in the wanted wait type
static int waits(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(waiter_thread, &info) == 0 && info.waitType == wanted_type();
}

// Creates its callback, then waits for the timeout in the kind of the case, with callbacks unless
// the scenario is plain. It records the wait's result, the time left and when it returned.
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    cb = sceKernelCreateCallback("notified", 0, notified_callback, NULL);
    const int plain = scenario == PLAIN;
    SceUInt timeout = WAIT_US;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    switch (kind) {
    case SIGNAL:
        result = plain ? sceKernelWaitSignal(0, 0, &timeout)
                       : sceKernelWaitSignalCB(0, 0, &timeout);
        break;
    case THREAD_END:
        result = plain ? sceKernelWaitThreadEnd(target, &end_status, &timeout)
                       : sceKernelWaitThreadEndCB(target, &end_status, &timeout);
        break;
    case DELAY:
        result = plain ? sceKernelDelayThread(WAIT_US) : sceKernelDelayThreadCB(WAIT_US);
        break;
    }
    elapsed = sceKernelGetProcessTimeWide() - start;
    returned_at = sceKernelGetProcessTimeWide();
    time_left = timeout;
    runs_at_return = callback_runs;
    if (plain) {
        check = sceKernelCheckCallback();
        runs_after_check = callback_runs;
    }
    sceKernelDeleteCallback(cb);
    return 0;
}

// Ends the wait of the kind with its event, a signal to T or the end of the target
static void send_event(void) {
    switch (kind) {
    case SIGNAL: sceKernelSendSignal(waiter_thread); break;
    case THREAD_END: sceKernelSignalSema(target_gate, 1); break;
    case DELAY: break;
    }
}

// Returns the name of the kind, with callbacks or plain in it
static const char *kind_name(void) {
    if (scenario == PLAIN) {
        switch (kind) {
        case SIGNAL: return "plain signal wait";
        case THREAD_END: return "plain thread end wait";
        case DELAY: return "plain delay";
        }
    }
    switch (kind) {
    case SIGNAL: return "signal wait with callbacks";
    case THREAD_END: return "thread end wait with callbacks";
    case DELAY: return "delay with callbacks";
    }
    return "";
}

// Runs one kind in one scenario with fresh objects, then prints what it saw
static void run_case(Scenario s, Kind k) {
    scenario = s;
    kind = k;
    hold = s == HELD || s == HELD_WITH_EVENT;
    cb = -1;
    callback_runs = 0;
    result = 0xDEAD;
    check = 0xDEAD;
    runs_at_return = 0xDEAD;
    runs_after_check = 0xDEAD;
    time_left = 0xDEAD;
    end_status = 0xDEAD;
    elapsed = 0;
    returned_at = 0;
    release_at = 0;
    callback_gate = sceKernelCreateSema("callback gate", 0, 0, 1, NULL);
    if (k == THREAD_END) {
        target_gate = sceKernelCreateSema("target gate", 0, 0, 1, NULL);
        target = test_start_thread(target_entry);
    }
    waiter_thread = test_start_thread(waiter);
    TEST_WAIT_FOR(waits());

    switch (s) {
    case NOTIFIED_EARLY:
    case PLAIN:
        sceKernelDelayThread(NOTIFY_EARLY_US);
        sceKernelNotifyCallback(cb, 1);
        break;
    case HELD_WITH_EVENT:
    case HELD:
        sceKernelNotifyCallback(cb, 1);
        TEST_WAIT_FOR(callback_runs > 0);
        sceKernelDelayThread(EVENT_US);
        if (s == HELD_WITH_EVENT)
            send_event();
        sceKernelDelayThread(RELEASE_US - EVENT_US);
        release_at = sceKernelGetProcessTimeWide();
        sceKernelSignalSema(callback_gate, 1);
        break;
    }

    test_join_thread(waiter_thread);
    if (k == THREAD_END) {
        sceKernelSignalSema(target_gate, 1);
        test_join_thread(target);
        sceKernelDeleteSema(target_gate);
    }
    sceKernelDeleteSema(callback_gate);

    const char *name = kind_name();
    switch (s) {
    case NOTIFIED_EARLY:
        PRINTF(result, "%s, notified at 500 ms: the result", name);
        PRINTF(elapsed >= MIN_LASTED_US && elapsed < MAX_EARLY_US,
               "%s, notified at 500 ms: it returned between 750 ms and 1250 ms", name);
        if (k != DELAY)
            PRINTF(time_left == 0, "%s, notified at 500 ms: the time left is 0", name);
        PRINTF(callback_runs, "%s, notified at 500 ms: runs of the callback", name);
        break;
    case HELD_WITH_EVENT:
        PRINTF(result, "%s, held, event at 1.25 s: the result", name);
        if (k != DELAY)
            PRINTF(time_left > 0, "%s, held, event at 1.25 s: the time left is above 0", name);
        if (k == THREAD_END)
            PRINTF(end_status, "%s, held, event at 1.25 s: the status of the end", name);
        PRINTF(callback_runs, "%s, held, event at 1.25 s: runs of the callback", name);
        break;
    case HELD:
        PRINTF(result, "%s, held, no event: the result", name);
        PRINTF(callback_runs, "%s, held, no event: runs of the callback", name);
        PRINTF(returned_at >= release_at && returned_at - release_at < NEAR_RELEASE_US,
               "%s, held, no event: it returned within 250 ms of the release", name);
        break;
    case PLAIN:
        PRINTF(elapsed >= MIN_LASTED_US, "%s, notified at 500 ms: it lasted at least 750 ms", name);
        PRINTF(result, "%s, notified at 500 ms: the result", name);
        PRINTF(runs_at_return, "%s, notified at 500 ms: runs of the callback when it returned",
               name);
        PRINTF(check, "%s, notified at 500 ms: the result of sceKernelCheckCallback", name);
        PRINTF(runs_after_check, "%s, notified at 500 ms: runs of the callback after the check",
               name);
        break;
    }
}

int main(void) {
    static const Scenario scenarios[] = { NOTIFIED_EARLY, HELD_WITH_EVENT, HELD, PLAIN };
    static const Kind kinds[] = { SIGNAL, THREAD_END, DELAY };
    for (unsigned i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++)
        for (unsigned j = 0; j < sizeof(kinds) / sizeof(kinds[0]); j++)
            run_case(scenarios[i], kinds[j]);

    test_exit();
    return 0;
}
