// What a callback that writes the caller's timeout variable during a wait with callbacks changes,
// for the kinds the model cb_wait_callback_writes_timeout doesn't cover. The model covers the
// semaphore, the event flag, the simple event and the message pipe receive. These cases cover the
// mutex, the lightweight mutex, the read lock, the write lock, the timer, the condition, the
// lightweight condition, the message pipe send, the thread signal and the thread end.
//
// A thread T creates a callback C, notifies it, which only leaves it pending since T isn't waiting,
// and waits with callbacks with a timeout in a variable. C runs at the start of the wait and writes
// that variable. Every case has fresh objects and runs T alone, the main thread waits for T to end.
// Two sets of cases:
//
// - C writes 12345 and gives T's object what T waits for, with a timeout of 1 s. These print what
//   the variable holds after the wait: about the 1000000 passed, the 12345 C wrote, or something
//   else.
// - C writes 1 and gives nothing, with a timeout of 0.5 s, so the wait times out. These print
//   whether it lasted at least 0.25 s, which it does if C's write didn't shorten the timeout.
//
// What T waits for, and what C does to give it, is the same as in cb_wait_zero_timeout_notify_more:
// the main thread releases the locks and the condition on C's request, and C releases the other
// objects itself. Every case also prints the wait's result and how often C ran.
//
// On a PS Vita TV on firmware 3.65 the waits C gives return 0 and leave about the timeout passed in
// the variable when the wait takes what it waits for at once. The lock waits do that. The message
// pipe send leaves C's 12345, as the model's message pipe receive does. The timer wait sleeps until
// its 400 ms timer fires, so it leaves about 600 ms. The condition waits lose the signal that C
// gives, since T isn't waiting on the condition yet, so they sleep and time out with 0 in the
// variable. The thread signal and thread end waits are the least certain: their timeout may be read
// after the callback ran, which would leave C's 12345 in it. The waits C gives nothing return
// 0x80028005 and last at least 0.25 s. C runs once in each case.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define WAIT_US 1000000
#define WRITTEN 12345
#define SHORT_WAIT_US 500000
#define SHORT_WRITTEN 1
#define LASTED_US 250000
#define MESSAGE_SIZE 4
#define CAPACITY 4096
#define TIMER_US 400000
#define NEAR_US 250000
#define CLOSE_US 10000

typedef enum Kind { MUTEX, LW_MUTEX, READ, WRITE, TIMER, COND, LW_COND, SEND, SIGNAL, END } Kind;

static Kind kind;
static int gives;
static SceUID object, mutex, gate, handoff, back, t_thread, helper;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static char fill[CAPACITY];
static SceUInt *volatile timeout_variable;
static volatile int callback_runs, wait_result;
static volatile SceUInt time_after;
static volatile SceUInt64 elapsed;

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
    sceKernelWaitSema(back, 1, &timeout);
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
}

// Writes the caller's timeout variable, then gives the wait what it waits for if the case says so
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    if (!gives) {
        *timeout_variable = SHORT_WRITTEN;
        return 0;
    }
    *timeout_variable = WRITTEN;
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

// Creates a callback, notifies it, and waits with callbacks
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    t_thread = sceKernelGetThreadId();
    const SceUID callback = sceKernelCreateCallback("writer", 0, callback_entry, NULL);
    sceKernelNotifyCallback(callback, 1);
    SceUInt timeout = gives ? WAIT_US : SHORT_WAIT_US;
    timeout_variable = &timeout;
    SceUInt32 bits = 0;
    SceUInt64 user_data = 0;
    char buffer[MESSAGE_SIZE] = { 'A', 'B', 'C', 'D' };
    if (kind == COND)
        sceKernelLockMutex(mutex, 1, NULL);
    if (kind == LW_COND)
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
    const SceUInt64 start = sceKernelGetProcessTimeWide();
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
    elapsed = sceKernelGetProcessTimeWide() - start;
    time_after = timeout;
    give_back(wait_result);
    sceKernelDeleteCallback(callback);
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

// Deletes what setup created, and lets the helper of a thread end case go
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

static void run_case(const char *name, Kind k, int give_it) {
    kind = k;
    gives = give_it;
    callback_runs = 0;
    wait_result = NOT_DONE;
    time_after = NOT_DONE;
    elapsed = 0;
    setup();

    const SceUID t = test_start_thread(t_entry);
    if (give_it && main_releases())
        serve_hand_off();
    test_join_thread(t);

    PRINTF(wait_result, "%s: the wait", name);
    PRINTF(callback_runs, "%s: runs of the callback", name);
    if (give_it) {
        PRINTF(time_after + NEAR_US >= WAIT_US, "%s: the timeout holds about the value passed", name);
        PRINTF(time_after + CLOSE_US >= WRITTEN && time_after <= WRITTEN + CLOSE_US,
            "%s: the timeout holds the callback's value", name);
    } else {
        PRINTF(elapsed >= LASTED_US, "%s: it lasted at least 0.25 s", name);
    }
    teardown();
}

int main(void) {
    run_case("mutex, the callback gives", MUTEX, 1);
    run_case("lightweight mutex, the callback gives", LW_MUTEX, 1);
    run_case("read lock, the callback gives", READ, 1);
    run_case("write lock, the callback gives", WRITE, 1);
    run_case("timer, the callback gives", TIMER, 1);
    run_case("condition, the callback gives", COND, 1);
    run_case("lightweight condition, the callback gives", LW_COND, 1);
    run_case("message pipe send, the callback gives", SEND, 1);
    run_case("thread signal, the callback gives", SIGNAL, 1);
    run_case("thread end, the callback gives", END, 1);

    run_case("mutex, the callback gives nothing", MUTEX, 0);
    run_case("lightweight mutex, the callback gives nothing", LW_MUTEX, 0);
    run_case("read lock, the callback gives nothing", READ, 0);
    run_case("write lock, the callback gives nothing", WRITE, 0);
    run_case("timer, the callback gives nothing", TIMER, 0);
    run_case("condition, the callback gives nothing", COND, 0);
    run_case("lightweight condition, the callback gives nothing", LW_COND, 0);
    run_case("message pipe send, the callback gives nothing", SEND, 0);
    run_case("thread signal, the callback gives nothing", SIGNAL, 0);
    run_case("thread end, the callback gives nothing", END, 0);

    test_exit();
    return 0;
}
