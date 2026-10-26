// What a wait with callbacks returns when its deadline is already gone and a callback is pending at
// the start that notifies itself again, for the kinds the model cb_wait_zero_timeout_notify doesn't
// cover. The model covers the semaphore, the event flag, the simple event and the message pipe
// receive. These cases cover the mutex, the lightweight mutex, the read lock, the write lock, the
// timer, the condition, the lightweight condition, the message pipe send, the thread signal and the
// thread end.
//
// A thread T creates a callback C and notifies it, which only leaves it pending since T isn't
// waiting. Then T waits with callbacks with a timeout of 0. C runs at the start of the wait, and
// its first run notifies C again, so a callback is pending once more after the take failed. Its
// second run, whenever it comes, gives T's object what T waits for. Every case has fresh objects
// and runs T alone. The main thread waits for T to end and then checks whether the object holds
// what C's second run gave.
//
// What T waits for, and what C's second run does about it:
//
// - mutex, lightweight mutex, read lock, write lock: the main thread holds the lock, a write lock
//   for a read lock wait and a read lock for a write lock wait. C can't unlock what the main thread
//   holds, so it asks the main thread to unlock it, and waits until the main thread did.
// - timer: the event of the timer is set to 50 ms, and C starts the timer.
// - condition, lightweight condition: T holds the mutex, and C asks the main thread to signal the
//   condition, the same way as for the locks.
// - message pipe send: the pipe is full, and C receives 4 bytes from it.
// - thread signal: C sends the signal to T.
// - thread end: T waits for a helper thread, which waits for a gate. C opens the gate and waits
//   until the helper ended.
//
// Each case prints the wait's result, the time left in the timeout variable, how often C ran, and
// whether the object holds what C's second run gave. The check is whether the lock can be taken
// once T ended, whether the timer's event is set, whether 4 bytes fit into the pipe, whether T
// still has a signal pending after its wait, whether the helper ended, and for the condition kinds
// whether the release was made, since a signal leaves nothing behind for a later check.
//
// On a PS Vita TV on firmware 3.65 the waits that run callbacks again before they return are the
// lock, timer and message pipe send waits. Those return 0x80028005 with the time left 0, C runs
// twice, and the object holds what C's second run gave. The condition, thread signal and thread end
// waits only run callbacks at the top of their loop, so they return 0x80028005 with the time left 0
// while C runs once, and nothing was given.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define MESSAGE_SIZE 4
#define CAPACITY 4096
#define TIMER_US 50000

typedef enum Kind { MUTEX, LW_MUTEX, READ, WRITE, TIMER, COND, LW_COND, SEND, SIGNAL, END } Kind;

static Kind kind;
static SceUID object, mutex, gate, handoff, back, t_thread, helper;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static char fill[CAPACITY];
static volatile SceUID callback;
static volatile int callback_runs, wait_result, gave, holds_signal;
static volatile SceUInt time_after;

// Whether the main thread releases what T waits for, since the callback can't do it
static int main_releases(void) {
    return kind == MUTEX || kind == LW_MUTEX || kind == READ || kind == WRITE || kind == COND || kind == LW_COND;
}

// Whether the helper of a thread end case has ended
static int helper_done(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(helper, &info) == 0 && info.status == SCE_THREAD_DORMANT;
}

// Waits for the gate, then returns
static int helper_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// Gives T's wait what it waits for. The main thread calls it, and so does C for the kinds it can
// release itself.
static void release(void) {
    SceSize size = 0;
    char buffer[MESSAGE_SIZE];
    switch (kind) {
    case MUTEX: sceKernelUnlockMutex(object, 1); break;
    case LW_MUTEX: sceKernelUnlockLwMutex(&lw_mutex, 1); break;
    case READ: sceKernelUnlockWriteRWLock(object); break;
    case WRITE: sceKernelUnlockReadRWLock(object); break;
    case TIMER: sceKernelStartTimer(object); break;
    case COND: sceKernelSignalCond(object); break;
    case LW_COND: sceKernelSignalLwCond(&lw_cond); break;
    // A blocking receive, since a try receive that leaves a sender waiting can hang the console
    case SEND: sceKernelReceiveMsgPipe(object, buffer, sizeof(buffer), MSG_PIPE_MODE_FULL, &size, NULL); break;
    case SIGNAL: sceKernelSendSignal(t_thread); break;
    case END: sceKernelSignalSema(gate, 1); break;
    }
}

// The callback asks the main thread to release, and waits until the main thread did
static void hand_off(void) {
    SceUInt timeout = TEST_WAIT_US;
    sceKernelSignalSema(handoff, 1);
    gave = sceKernelWaitSema(back, 1, &timeout) == 0;
}

// The main thread releases for a hand off, if the callback asked for one
static void serve_hand_off(void) {
    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelWaitSema(handoff, 1, &timeout) == 0) {
        release();
        sceKernelSignalSema(back, 1);
    }
}

// The callback's second run gives T's wait what it waits for, by a hand off or by itself
static void give_from_callback(void) {
    if (main_releases()) {
        hand_off();
        return;
    }
    release();
    if (kind == END)
        TEST_WAIT_FOR(helper_done());
    gave = 1;
}

// The first run notifies the callback again, and the second gives T's wait what it waits for
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    if (callback_runs == 1)
        sceKernelNotifyCallback(callback, 1);
    else if (callback_runs == 2)
        give_from_callback();
    return 0;
}

// Unlocks what a wait took, and the mutex a condition wait leaves locked
static void give_back(int result) {
    if (result == 0) {
        switch (kind) {
        case MUTEX: sceKernelUnlockMutex(object, 1); break;
        case LW_MUTEX: sceKernelUnlockLwMutex(&lw_mutex, 1); break;
        case READ: sceKernelUnlockReadRWLock(object); break;
        case WRITE: sceKernelUnlockWriteRWLock(object); break;
        default: break;
        }
    }
    if (kind == COND)
        sceKernelUnlockMutex(mutex, 1);
    if (kind == LW_COND)
        sceKernelUnlockLwMutex(&lw_mutex, 1);
}

// Creates a callback, notifies it, and waits with callbacks with a timeout of 0
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    t_thread = sceKernelGetThreadId();
    callback = sceKernelCreateCallback("zero", 0, callback_entry, NULL);
    sceKernelNotifyCallback(callback, 1);
    SceUInt timeout = 0;
    SceUInt32 bits = 0;
    SceUInt64 user_data = 0;
    char buffer[MESSAGE_SIZE] = { 'A', 'B', 'C', 'D' };
    if (kind == COND)
        sceKernelLockMutex(mutex, 1, NULL);
    if (kind == LW_COND)
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
    switch (kind) {
    case MUTEX: wait_result = sceKernelLockMutexCB(object, 1, &timeout); break;
    case LW_MUTEX: wait_result = sceKernelLockLwMutexCB(&lw_mutex, 1, &timeout); break;
    case READ: wait_result = sceKernelLockReadRWLockCB(object, &timeout); break;
    case WRITE: wait_result = sceKernelLockWriteRWLockCB(object, &timeout); break;
    case TIMER: wait_result = sceKernelWaitEventCB(object, EVENT_TIMER, &bits, &user_data, &timeout); break;
    case COND: wait_result = sceKernelWaitCondCB(object, &timeout); break;
    case LW_COND: wait_result = sceKernelWaitLwCondCB(&lw_cond, &timeout); break;
    case SEND:
        wait_result = sceKernelSendMsgPipeCB(object, buffer, sizeof(buffer), MSG_PIPE_MODE_FULL, NULL, &timeout);
        break;
    case SIGNAL: wait_result = sceKernelWaitSignalCB(0, 0, &timeout); break;
    case END: wait_result = sceKernelWaitThreadEndCB(helper, NULL, &timeout); break;
    }
    time_after = timeout;
    give_back(wait_result);
    if (kind == SIGNAL) {
        SceUInt32 none = 0;
        holds_signal = sceKernelWaitSignal(0, 0, &none) == 0;
    }
    sceKernelDeleteCallback(callback);
    return 0;
}

// Returns whether the object holds what C's second run gave, read after T ended
static int holds_gift(void) {
    SceUInt32 bits = 0;
    SceUInt64 user_data = 0;
    SceSize size = 0;
    char buffer[MESSAGE_SIZE] = { 'E', 'F', 'G', 'H' };
    int available = 0;
    switch (kind) {
    case MUTEX:
        available = sceKernelTryLockMutex(object, 1) == 0;
        if (available)
            sceKernelUnlockMutex(object, 1);
        return available;
    case LW_MUTEX:
        available = sceKernelTryLockLwMutex(&lw_mutex, 1) == 0;
        if (available)
            sceKernelUnlockLwMutex(&lw_mutex, 1);
        return available;
    case READ:
        available = sceKernelTryLockReadRWLock(object) == 0;
        if (available)
            sceKernelUnlockReadRWLock(object);
        return available;
    case WRITE:
        available = sceKernelTryLockWriteRWLock(object) == 0;
        if (available)
            sceKernelUnlockWriteRWLock(object);
        return available;
    case TIMER:
        if (gave)
            TEST_WAIT_FOR(sceKernelPollEvent(object, EVENT_TIMER, &bits, &user_data) == 0);
        return gave && sceKernelPollEvent(object, EVENT_TIMER, &bits, &user_data) == 0;
    case COND:
    case LW_COND: return gave;
    case SEND: return sceKernelTrySendMsgPipe(object, buffer, sizeof(buffer), MSG_PIPE_MODE_FULL, &size) == 0;
    case SIGNAL: return holds_signal;
    case END: return helper_done();
    }
    return 0;
}

// Creates the object of the case, with the lock held by the main thread where the case needs it
static void setup(void) {
    SceUInt timeout = TEST_WAIT_US;
    SceUInt64 interval = TIMER_US;
    handoff = sceKernelCreateSema("handoff", 0, 0, 1, NULL);
    back = sceKernelCreateSema("back", 0, 0, 1, NULL);
    switch (kind) {
    case MUTEX:
        object = sceKernelCreateMutex("mutex", 0, 0, NULL);
        sceKernelLockMutex(object, 1, NULL);
        break;
    case LW_MUTEX:
        sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 0, NULL);
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        break;
    case READ:
        object = sceKernelCreateRWLock("rwlock", 0, NULL);
        sceKernelLockWriteRWLock(object, &timeout);
        break;
    case WRITE:
        object = sceKernelCreateRWLock("rwlock", 0, NULL);
        sceKernelLockReadRWLock(object, &timeout);
        break;
    case TIMER:
        object = sceKernelCreateTimer("timer", 0, NULL);
        sceKernelSetTimerEvent(object, TIMER_TYPE_SET_EVENT, &interval, 0);
        break;
    case COND:
        mutex = sceKernelCreateMutex("cond mutex", 0, 0, NULL);
        object = sceKernelCreateCond("cond", 0, mutex, NULL);
        break;
    case LW_COND:
        sceKernelCreateLwMutex(&lw_mutex, "lw cond mutex", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "lw cond", 0, &lw_mutex, NULL);
        break;
    case SEND:
        object = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
        sceKernelSendMsgPipe(object, fill, CAPACITY, MSG_PIPE_MODE_FULL, NULL, &timeout);
        break;
    case SIGNAL: break;
    case END:
        gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
        helper = test_start_thread(helper_entry);
        break;
    }
}

// Deletes what setup created
static void teardown(void) {
    switch (kind) {
    case MUTEX:
        sceKernelUnlockMutex(object, 1);
        sceKernelDeleteMutex(object);
        break;
    case LW_MUTEX:
        sceKernelUnlockLwMutex(&lw_mutex, 1);
        sceKernelDeleteLwMutex(&lw_mutex);
        break;
    case READ:
    case WRITE:
        sceKernelUnlockWriteRWLock(object);
        sceKernelUnlockReadRWLock(object);
        sceKernelDeleteRWLock(object);
        break;
    case TIMER:
        sceKernelStopTimer(object);
        sceKernelDeleteTimer(object);
        break;
    case COND:
        sceKernelDeleteCond(object);
        sceKernelDeleteMutex(mutex);
        break;
    case LW_COND:
        sceKernelDeleteLwCond(&lw_cond);
        sceKernelDeleteLwMutex(&lw_mutex);
        break;
    case SEND: sceKernelDeleteMsgPipe(object); break;
    case SIGNAL: break;
    case END:
        sceKernelSignalSema(gate, 1);
        test_join_thread(helper);
        sceKernelDeleteSema(gate);
        break;
    }
    sceKernelDeleteSema(handoff);
    sceKernelDeleteSema(back);
}

static void run_case(const char *name, Kind k) {
    kind = k;
    callback_runs = 0;
    gave = 0;
    holds_signal = 0;
    wait_result = NOT_DONE;
    time_after = NOT_DONE;
    setup();

    const SceUID t = test_start_thread(t_entry);
    if (main_releases())
        serve_hand_off();
    test_join_thread(t);

    PRINTF(wait_result, "%s: the wait", name);
    PRINTF(time_after, "%s: the time left", name);
    PRINTF(callback_runs, "%s: runs of the callback", name);
    PRINTF(holds_gift(), "%s: the object holds what the second run gave", name);
    teardown();
}

int main(void) {
    run_case("mutex", MUTEX);
    run_case("lightweight mutex", LW_MUTEX);
    run_case("read lock", READ);
    run_case("write lock", WRITE);
    run_case("timer", TIMER);
    run_case("condition", COND);
    run_case("lightweight condition", LW_COND);
    run_case("message pipe send", SEND);
    run_case("thread signal", SIGNAL);
    run_case("thread end", END);

    test_exit();
    return 0;
}
