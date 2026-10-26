// What the wait of a thread T with callbacks shows right after sceKernelNotifyCallback of T's
// callback, before T can run, and again once T ran the callback and waits again. This is
// callback_notify_departure_state for the kinds it leaves out: a timer, a delay with callbacks, a
// thread signal wait and a thread end wait. The model covers the semaphore, event flag, mutex,
// lightweight mutex, rwlock, simple event, message pipe, condition and lightweight condition kinds,
// with a semaphore wait without callbacks as the reference.
//
// Every thread is pinned to core 0. The main thread runs at 0x60 and T at 0xB0, so T stays ready
// until the main thread blocks. T creates its callback in its own thread before it waits, and the
// callback counts its runs. Every wait has a timeout of 6 seconds, except the delay, which is 2
// seconds long. Every case has a fresh T and a fresh timer, target or gate. The main thread waits
// until T waits, reads the timer's count, notifies the callback, and blocks until the callback ran
// and T waits again. Then it reads T's state, ends T's wait, joins T, and prints. The prints come
// last, since a print blocks the main thread and lets T run. Cases:
// - a timer, T in sceKernelWaitEventCB for EVENT_TIMER. The timer is set to fire after 100 ms, and
//   the main thread starts it to end T's wait, after T waits again
// - a delay with callbacks, T in sceKernelDelayThreadCB. Nothing ends it early, so the case only
//   joins T, which returns when its delay ends
// - a thread signal wait, T in sceKernelWaitSignalCB, ended by sceKernelSendSignal
// - a thread end wait, T in sceKernelWaitThreadEndCB on a target thread that waits on a gate
//   semaphore. The main thread signals the gate, so the target returns and T's wait ends
//
// Some systems run a woken thread at once, so a read right after the notify can see T either before
// or after it ran, and the main thread doesn't read T's state there. The callback runs only once T
// left the timer's queue, and T is running then, so the first run of the callback reads the timer's
// count and T's own wait state. Each case prints:
// - whether T waits with the wait type of its kind before the notify, and for the timer the timer's
//   count of waiting threads then
// - the result of the notify
// - when T runs its callback: T's wait type with the callback bit masked off, whether T's wait has
//   the callback bit, and for the timer the timer's count of waiting threads
// - how often the callback ran once T waits again, then whether T waits, its wait type with the
//   callback bit masked off, whether that has the callback bit, and for the timer its count of
//   waiting threads and whether T's wait UID is the timer
// - the call that ends T's wait, for the timer, the signal and the thread end, and T's wait result
// - for the timer, the signal and the thread end, whether T's time left was above 1 s when its wait
//   returned, which is the case when the wait didn't time out early and still had most of its time
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define LONG_TIMEOUT (3 * TEST_WAIT_US)
#define DELAY_US 2000000
#define TIMER_INTERVAL_US 100000
#define LONG_LEFT_US 1000000

typedef enum Kind {
    TIMER,
    DELAY,
    SIGNAL,
    THREAD_END
} Kind;

typedef struct Case {
    const char *name;
    Kind kind;
    // The wait type of T's wait without WAIT_TYPE_CB
    SceUInt32 wait_type;
} Case;

// What the main thread reads of T once it waits again
typedef struct State {
    int waiting;
    SceUInt32 wait_type;
    int callback_bit;
    int on_timer;
} State;

static Kind current;
static SceUID timer, gate, target, waiter_thread, callback;
static volatile int result, callback_runs;
static volatile SceUInt time_left;
// What the first run of the callback reads: T's wait type with the callback bit masked off, whether
// the callback bit is set, and the timer's count of waiting threads
static volatile SceUInt32 callback_wait_type;
static volatile int callback_bit, callback_timer_waiters;

// Returns the timer's count of waiting threads, or NOT_DONE
static int timer_waiters(void) {
    EventInfo info = { .size = sizeof(info) };
    return sceKernelGetEventInfo(timer, &info) == 0 ? (int)info.numWaitThreads : NOT_DONE;
}

// Counts its runs, and records T's wait state and the timer's count at the first run
static int notified_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    if (callback_runs == 0) {
        SceKernelThreadInfo info = { .size = sizeof(info) };
        if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &info) == 0) {
            callback_wait_type = info.waitType & ~WAIT_TYPE_CB;
            callback_bit = (info.waitType & WAIT_TYPE_CB) != 0;
        }
        callback_timer_waiters = current == TIMER ? timer_waiters() : NOT_DONE;
    }
    callback_runs++;
    return 0;
}

// Waits for the gate, then returns, so the thread end wait ends
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = LONG_TIMEOUT;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// Creates its callback, then waits with callbacks in the kind of the current case
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = LONG_TIMEOUT;
    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    callback = sceKernelCreateCallback("notified", 0, notified_callback, NULL);
    switch (current) {
    case TIMER: result = sceKernelWaitEventCB(timer, EVENT_TIMER, &pattern, &user_data, &timeout); break;
    case DELAY: result = sceKernelDelayThreadCB(DELAY_US); break;
    case SIGNAL: result = sceKernelWaitSignalCB(0, 0, &timeout); break;
    case THREAD_END: result = sceKernelWaitThreadEndCB(target, NULL, &timeout); break;
    }
    time_left = timeout;
    sceKernelDeleteCallback(callback);
    return 0;
}

// Returns whether T waits with callbacks in the wait type of the case, on the timer for a timer
static int waits(SceUInt32 wait_type) {
    if (current == TIMER)
        return test_in_wait(waiter_thread, wait_type | WAIT_TYPE_CB, timer);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(waiter_thread, &info) == 0 && info.waitType == (wait_type | WAIT_TYPE_CB);
}

// Reads T's wait state once it waits again
static State read_state(Kind kind) {
    State s = { .waiting = NOT_DONE, .wait_type = NOT_DONE, .callback_bit = NOT_DONE, .on_timer = NOT_DONE };
    SceKernelThreadInfo info = { .size = sizeof(info) };
    if (sceKernelGetThreadInfo(waiter_thread, &info) == 0) {
        s.waiting = info.status == SCE_THREAD_WAITING;
        s.wait_type = info.waitType & ~WAIT_TYPE_CB;
        s.callback_bit = (info.waitType & WAIT_TYPE_CB) != 0;
        s.on_timer = kind == TIMER ? info.waitId == timer : NOT_DONE;
    }
    return s;
}

static void print_state(const Case *c, const State *s) {
    PRINTF(s->waiting, "%s: T is waiting after T waits again", c->name);
    PRINTF(s->wait_type, "%s: T's wait type after T waits again", c->name);
    PRINTF(s->callback_bit, "%s: T's wait has the callback bit after T waits again", c->name);
    if (c->kind == TIMER)
        PRINTF(s->on_timer, "%s: T's wait UID is the timer after T waits again", c->name);
}

// Creates the timer, or the target of a thread end wait, which waits for the gate
static void create_object(Kind kind) {
    switch (kind) {
    case TIMER: {
        timer = sceKernelCreateTimer("timer", 0, NULL);
        SceUInt64 interval = TIMER_INTERVAL_US;
        sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
        break;
    }
    case THREAD_END:
        gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
        target = sceKernelCreateThread("target", target_entry, WAITER_PRIORITY, 0x4000, 0,
                                       SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(target, 0, NULL);
        break;
    case DELAY:
    case SIGNAL: break;
    }
}

// Ends T's wait and returns the result of the call that does it, or NOT_DONE if nothing ends it
static int end_wait(Kind kind) {
    switch (kind) {
    case TIMER: return sceKernelStartTimer(timer);
    case SIGNAL: return sceKernelSendSignal(waiter_thread);
    case THREAD_END: return sceKernelSignalSema(gate, 1);
    case DELAY: break;
    }
    return NOT_DONE;
}

static void delete_object(Kind kind) {
    switch (kind) {
    case TIMER: sceKernelDeleteTimer(timer); break;
    case THREAD_END: sceKernelDeleteSema(gate); break;
    case DELAY:
    case SIGNAL: break;
    }
}

static void run_case(const Case *c) {
    current = c->kind;
    callback = -1;
    callback_runs = 0;
    result = NOT_DONE;
    time_left = NOT_DONE;
    callback_wait_type = NOT_DONE;
    callback_bit = NOT_DONE;
    callback_timer_waiters = NOT_DONE;
    create_object(c->kind);
    waiter_thread = sceKernelCreateThread("t", waiter, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(waiter_thread, 0, NULL);
    TEST_WAIT_FOR(waits(c->wait_type));
    const int waits_before = waits(c->wait_type);
    const int before_waiters = c->kind == TIMER ? timer_waiters() : NOT_DONE;

    const int notify = sceKernelNotifyCallback(callback, 1);

    TEST_WAIT_FOR(callback_runs > 0 && waits(c->wait_type) && (c->kind != TIMER || timer_waiters() == 1));
    const int runs = callback_runs;
    const State again = read_state(c->kind);
    const int again_waiters = c->kind == TIMER ? timer_waiters() : NOT_DONE;

    const int end = end_wait(c->kind);
    test_join_thread(waiter_thread);
    if (c->kind == THREAD_END)
        test_join_thread(target);
    delete_object(c->kind);
    const int long_left = time_left > LONG_LEFT_US;

    PRINTF(waits_before, "%s: T waits before the notify", c->name);
    if (c->kind == TIMER)
        PRINTF(before_waiters, "%s: the timer's waiting threads before the notify", c->name);
    PRINTF(notify, "%s: the notify", c->name);
    PRINTF(callback_wait_type, "%s: T's wait type when it runs its callback", c->name);
    PRINTF(callback_bit, "%s: T's wait has the callback bit when it runs its callback", c->name);
    if (c->kind == TIMER)
        PRINTF(callback_timer_waiters, "%s: the timer's waiting threads when T runs its callback", c->name);
    PRINTF(runs, "%s: runs of the callback once T waits again", c->name);
    print_state(c, &again);
    if (c->kind == TIMER)
        PRINTF(again_waiters, "%s: the timer's waiting threads after T waits again", c->name);
    if (end != NOT_DONE)
        PRINTF(end, "%s: the call that ends T's wait", c->name);
    PRINTF(result, "%s: T's wait result", c->name);
    if (c->kind == TIMER || c->kind == SIGNAL || c->kind == THREAD_END)
        PRINTF(long_left, "%s: T's time left is above 1 s when its wait returned", c->name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    static const Case cases[] = {
        { "timer", TIMER, WAIT_TYPE_EVENT },
        { "delay with callbacks", DELAY, WAIT_TYPE_DELAY },
        { "thread signal wait", SIGNAL, WAIT_TYPE_SIGNAL },
        { "thread end wait", THREAD_END, WAIT_TYPE_THREAD_END },
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        run_case(&cases[i]);

    test_exit();
    return 0;
}
