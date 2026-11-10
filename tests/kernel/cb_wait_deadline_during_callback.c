// What a wait with callbacks does with its deadline when a callback runs during the wait, and what
// a plain wait does with a notify. The waits are a signal wait, a thread end wait and a delay, each
// with a timeout of 1 second, and every case has a fresh thread, callback, gate and target. Each
// scenario is run for each kind. The three kinds of a scenario run at the same time: their threads
// meet at a start gate, so the three waits start together, and the values are printed once all
// three ended. callback_outer_timeout covers the delay and the semaphore, this covers the other two
// kinds. The scenarios:
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

#define KIND_COUNT 3

// The objects and the values of one kind in the current scenario
typedef struct Case {
    Kind kind;
    SceUID cb, callback_gate, target_gate, target, waiter_thread;
    volatile int callback_runs;
    volatile int result, check, runs_at_return, runs_after_check;
    volatile SceUInt time_left;
    volatile SceUInt64 elapsed, returned_at, release_at;
    int end_status;
} Case;

static Scenario scenario;
static SceUID start_gate;
static volatile int hold;
static Case cases[KIND_COUNT];

// Counts its runs, and blocks until the test releases it when the scenario holds it
static int notified_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    Case *c = common;
    c->callback_runs++;
    if (hold) {
        SceUInt timeout = LONG_TIMEOUT;
        sceKernelWaitSema(c->callback_gate, 1, &timeout);
    }
    return 0;
}

// Waits for the target gate of its case, then returns 0x5A, which the end waits get as the status
static int target_entry(SceSize args, void *argp) {
    (void)args;
    SceUInt timeout = LONG_TIMEOUT;
    sceKernelWaitSema(cases[*(const int *)argp].target_gate, 1, &timeout);
    return 0x5A;
}

// Returns the wait type of the kind, with the callback bit unless the scenario is plain
static SceUInt32 wanted_type(Kind kind) {
    SceUInt32 type = WAIT_TYPE_DELAY;
    if (kind == SIGNAL)
        type = WAIT_TYPE_SIGNAL;
    else if (kind == THREAD_END)
        type = WAIT_TYPE_THREAD_END;
    return scenario == PLAIN ? type : type | WAIT_TYPE_CB;
}

// Returns whether T of the case waits in the wanted wait type
static int waits(const Case *c) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(c->waiter_thread, &info) == 0 && info.waitType == wanted_type(c->kind);
}

// Returns how many threads wait for the start gate
static int at_start_gate(void) {
    SceKernelSemaInfo info = { .size = sizeof(info) };
    return sceKernelGetSemaInfo(start_gate, &info) == 0 ? info.numWaitThreads : 0;
}

// Creates its callback and waits for the start gate, then waits for the timeout in the kind of its
// case, with callbacks unless the scenario is plain. It records the wait's result, the time left
// and when it returned.
static int waiter(SceSize args, void *argp) {
    (void)args;
    Case *c = &cases[*(const int *)argp];
    c->cb = sceKernelCreateCallback("notified", 0, notified_callback, c);
    SceUInt start_timeout = LONG_TIMEOUT;
    sceKernelWaitSema(start_gate, 1, &start_timeout);
    const int plain = scenario == PLAIN;
    SceUInt timeout = WAIT_US;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    switch (c->kind) {
    case SIGNAL:
        c->result = plain ? sceKernelWaitSignal(0, 0, &timeout)
                          : sceKernelWaitSignalCB(0, 0, &timeout);
        break;
    case THREAD_END:
        c->result = plain ? sceKernelWaitThreadEnd(c->target, &c->end_status, &timeout)
                          : sceKernelWaitThreadEndCB(c->target, &c->end_status, &timeout);
        break;
    case DELAY:
        c->result = plain ? sceKernelDelayThread(WAIT_US) : sceKernelDelayThreadCB(WAIT_US);
        break;
    }
    c->elapsed = sceKernelGetProcessTimeWide() - start;
    c->returned_at = sceKernelGetProcessTimeWide();
    c->time_left = timeout;
    c->runs_at_return = c->callback_runs;
    if (plain) {
        c->check = sceKernelCheckCallback();
        c->runs_after_check = c->callback_runs;
    }
    sceKernelDeleteCallback(c->cb);
    return 0;
}

// Ends the wait of the case with its event, a signal to T or the end of the target
static void send_event(const Case *c) {
    switch (c->kind) {
    case SIGNAL: sceKernelSendSignal(c->waiter_thread); break;
    case THREAD_END: sceKernelSignalSema(c->target_gate, 1); break;
    case DELAY: break;
    }
}

// Returns the name of the kind, with callbacks or plain in it
static const char *kind_name(Kind kind) {
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

// Prints what the case saw in the current scenario
static void print_case(const Case *c) {
    const Kind k = c->kind;
    const char *name = kind_name(k);
    switch (scenario) {
    case NOTIFIED_EARLY:
        PRINTF(c->result, "%s, notified at 500 ms: the result", name);
        PRINTF(c->elapsed >= MIN_LASTED_US && c->elapsed < MAX_EARLY_US,
               "%s, notified at 500 ms: it returned between 750 ms and 1250 ms", name);
        if (k != DELAY)
            PRINTF(c->time_left == 0, "%s, notified at 500 ms: the time left is 0", name);
        PRINTF(c->callback_runs, "%s, notified at 500 ms: runs of the callback", name);
        break;
    case HELD_WITH_EVENT:
        PRINTF(c->result, "%s, held, event at 1.25 s: the result", name);
        if (k != DELAY)
            PRINTF(c->time_left > 0, "%s, held, event at 1.25 s: the time left is above 0", name);
        if (k == THREAD_END)
            PRINTF(c->end_status, "%s, held, event at 1.25 s: the status of the end", name);
        PRINTF(c->callback_runs, "%s, held, event at 1.25 s: runs of the callback", name);
        break;
    case HELD:
        PRINTF(c->result, "%s, held, no event: the result", name);
        PRINTF(c->callback_runs, "%s, held, no event: runs of the callback", name);
        PRINTF(c->returned_at >= c->release_at && c->returned_at - c->release_at < NEAR_RELEASE_US,
               "%s, held, no event: it returned within 250 ms of the release", name);
        break;
    case PLAIN:
        PRINTF(c->elapsed >= MIN_LASTED_US, "%s, notified at 500 ms: it lasted at least 750 ms", name);
        PRINTF(c->result, "%s, notified at 500 ms: the result", name);
        PRINTF(c->runs_at_return, "%s, notified at 500 ms: runs of the callback when it returned",
               name);
        PRINTF(c->check, "%s, notified at 500 ms: the result of sceKernelCheckCallback", name);
        PRINTF(c->runs_after_check, "%s, notified at 500 ms: runs of the callback after the check",
               name);
        break;
    }
}

// Runs the three kinds in one scenario at the same time with fresh objects, then prints what each
// saw. Every kind keeps the timeline it would have alone, counted from when all three wait.
static void run_scenario(Scenario s) {
    scenario = s;
    hold = s == HELD || s == HELD_WITH_EVENT;
    start_gate = sceKernelCreateSema("start gate", 0, 0, KIND_COUNT, NULL);
    for (int k = 0; k < KIND_COUNT; k++) {
        Case *c = &cases[k];
        c->kind = k;
        c->cb = -1;
        c->callback_runs = 0;
        c->result = 0xDEAD;
        c->check = 0xDEAD;
        c->runs_at_return = 0xDEAD;
        c->runs_after_check = 0xDEAD;
        c->time_left = 0xDEAD;
        c->end_status = 0xDEAD;
        c->elapsed = 0;
        c->returned_at = 0;
        c->release_at = 0;
        c->callback_gate = sceKernelCreateSema("callback gate", 0, 0, 1, NULL);
        if (k == THREAD_END) {
            c->target_gate = sceKernelCreateSema("target gate", 0, 0, 1, NULL);
            c->target = test_start_thread_arg(target_entry, k);
        }
        c->waiter_thread = test_start_thread_arg(waiter, k);
    }
    TEST_WAIT_FOR(at_start_gate() == KIND_COUNT);
    sceKernelSignalSema(start_gate, KIND_COUNT);
    for (int k = 0; k < KIND_COUNT; k++)
        TEST_WAIT_FOR(waits(&cases[k]));

    switch (s) {
    case NOTIFIED_EARLY:
    case PLAIN:
        sceKernelDelayThread(NOTIFY_EARLY_US);
        for (int k = 0; k < KIND_COUNT; k++)
            sceKernelNotifyCallback(cases[k].cb, 1);
        break;
    case HELD_WITH_EVENT:
    case HELD:
        for (int k = 0; k < KIND_COUNT; k++)
            sceKernelNotifyCallback(cases[k].cb, 1);
        for (int k = 0; k < KIND_COUNT; k++)
            TEST_WAIT_FOR(cases[k].callback_runs > 0);
        sceKernelDelayThread(EVENT_US);
        if (s == HELD_WITH_EVENT)
            for (int k = 0; k < KIND_COUNT; k++)
                send_event(&cases[k]);
        sceKernelDelayThread(RELEASE_US - EVENT_US);
        for (int k = 0; k < KIND_COUNT; k++) {
            cases[k].release_at = sceKernelGetProcessTimeWide();
            sceKernelSignalSema(cases[k].callback_gate, 1);
        }
        break;
    }

    for (int k = 0; k < KIND_COUNT; k++) {
        Case *c = &cases[k];
        test_join_thread(c->waiter_thread);
        if (k == THREAD_END) {
            sceKernelSignalSema(c->target_gate, 1);
            test_join_thread(c->target);
            sceKernelDeleteSema(c->target_gate);
        }
        sceKernelDeleteSema(c->callback_gate);
    }
    sceKernelDeleteSema(start_gate);

    for (int k = 0; k < KIND_COUNT; k++)
        print_case(&cases[k]);
}

int main(void) {
    static const Scenario scenarios[] = { NOTIFIED_EARLY, HELD_WITH_EVENT, HELD, PLAIN };
    for (unsigned i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++)
        run_scenario(scenarios[i]);

    test_exit();
    return 0;
}
