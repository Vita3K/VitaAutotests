// Whether the run of a callback pending when a wait with callbacks starts counts against the wait's
// timeout, for the kinds the model cb_wait_initial_callback_deadline doesn't cover. The model
// covers the semaphore, the event flag, the simple event and the message pipe receive. These cases
// cover the mutex, the lightweight mutex, the read lock, the write lock, the timer, the condition,
// the lightweight condition, the message pipe send, the thread signal and the thread end.
//
// A thread T creates a callback C whose run delays T for 1 s, notifies it, which only leaves it
// pending since T isn't waiting, and waits with callbacks with a timeout of 0.5 s in a variable.
// Nothing ever gives what T waits for, so every wait times out. C runs at the start of the wait. A
// wait that built its deadline before running C finds it gone once C returned and times out right
// away, about 1 s after it started. One that built it after C waits the whole timeout too, about
// 1.5 s. Every case has fresh objects and runs T alone, the main thread waits for T to end. What
// each kind waits on:
//
// - mutex, lightweight mutex, read lock, write lock: the main thread holds the lock, a write lock
//   for a read lock wait and a read lock for a write lock wait.
// - timer: a timer that is never started.
// - condition, lightweight condition: T holds the mutex of the condition.
// - message pipe send: a full pipe, so 4 bytes don't fit.
// - thread signal: a signal nothing sends.
// - thread end: a helper thread that waits for a gate, which the main thread opens after T ended.
//
// Each case prints the wait's result, the time left in the timeout variable, how often C ran, and
// whether the wait returned less than 1.25 s after it started.
//
// On a PS Vita TV on firmware 3.65 the waits return 0x80028005 with the time left 0 less than 1.25
// s after they started, and C runs once in each, as in the model. The thread signal and thread end
// waits are the least certain of these, since the timeout of those waits may be read after the
// callback ran.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define CALLBACK_US 1000000
#define WAIT_US 500000
#define LIMIT_US 1250000
#define MESSAGE_SIZE 4
#define CAPACITY 4096

typedef enum Kind { MUTEX, LW_MUTEX, READ, WRITE, TIMER, COND, LW_COND, SEND, SIGNAL, END } Kind;

static Kind kind;
static SceUID object, mutex, gate, helper;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static char fill[CAPACITY];
static volatile int callback_runs, wait_result;
static volatile SceUInt time_after;
static volatile SceUInt64 elapsed;

// Waits for the gate, then returns
static int helper_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// Keeps T busy for longer than its wait's timeout
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    sceKernelDelayThread(CALLBACK_US);
    return 0;
}

// Creates a callback, notifies it, and waits with callbacks on an object nothing gives
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    const SceUID callback = sceKernelCreateCallback("long", 0, callback_entry, NULL);
    sceKernelNotifyCallback(callback, 1);
    SceUInt timeout = WAIT_US;
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
    if (kind == COND)
        sceKernelUnlockMutex(mutex, 1);
    if (kind == LW_COND)
        sceKernelUnlockLwMutex(&lw_mutex, 1);
    sceKernelDeleteCallback(callback);
    return 0;
}

// Creates the object of the case, with the lock held by the main thread where the case needs it
static void setup(void) {
    SceUInt timeout = TEST_WAIT_US;
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
    case TIMER: object = sceKernelCreateTimer("timer", 0, NULL); break;
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
    case TIMER: sceKernelDeleteTimer(object); break;
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
}

static void run_case(const char *name, Kind k) {
    kind = k;
    callback_runs = 0;
    wait_result = NOT_DONE;
    time_after = NOT_DONE;
    elapsed = 0;
    setup();

    test_join_thread(test_start_thread(t_entry));

    PRINTF(wait_result, "%s: the wait", name);
    PRINTF(time_after, "%s: the time left", name);
    PRINTF(callback_runs, "%s: runs of the callback", name);
    PRINTF(elapsed < LIMIT_US, "%s: it returned less than 1.25 s after it started", name);
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
