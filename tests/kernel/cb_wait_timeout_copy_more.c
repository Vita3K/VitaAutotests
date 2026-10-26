// What the timeout variable of a wait with callbacks holds while the callbacks run, and after the
// call returns, for the kinds the model cb_wait_timeout_copy leaves out. The model covers the
// semaphore, event flag, mutex, lightweight mutex, rwlock write lock, condition, simple event and
// message pipe receive waits. A thread T creates a callback, puts 2000000 in a global timeout
// variable and makes the wait with callbacks, passing that variable as the timeout. Once T waits,
// the main thread notifies the callback. The callback records the value of the timeout variable and
// sets a flag. The main thread waits for the flag and for T to be back in its wait, releases T and
// joins it. After the join it reads the timeout variable again. Every case runs on fresh objects
// and a fresh T. The cases differ in the wait and the release:
// - a read lock while the main thread holds the write lock, sceKernelLockReadRWLockCB, and the main
//   thread unlocks the write lock
// - a send of 4 bytes into a full message pipe, sceKernelSendMsgPipeCB in the FULL mode, and the
//   main thread receives 4 bytes from the pipe
// - a timer that isn't started, sceKernelWaitEventCB for EVENT_TIMER, and the main thread starts it
// - a lightweight condition that T waits on while it holds the lightweight mutex,
//   sceKernelWaitLwCondCB, and the main thread signals the condition
// - a thread signal, sceKernelWaitSignalCB(0, 0, &timeout), and the main thread sends T a signal
// - a thread end, sceKernelWaitThreadEndCB on a helper that waits on a gate, and the main thread
//   signals the gate, so the helper ends
// - multiple events, sceKernelWaitMultipleEventsCB on a simple event that holds no bits. T waits at
//   a gate until the callback is notified, so the callback is pending before the wait and is not
//   notified during it, since a notify while a thread waits in this call hangs the console. The
//   main thread sets the event's bit once T is back in the wait
// Each case prints whether T waits with callbacks before it is released, T's wait result, whether
// the timeout that the callback saw is the one passed and whether it is above 1 s, and whether the
// timeout after the call is below the one passed and above 1 s.
//
// On a PS Vita TV on firmware 3.65 every wait returns 0 except the multiple events wait, which
// returns 1, the count of result entries it wrote. The timeout after the call is below the one
// passed and above 1 s. The callback of the object waits sees the one passed, since those
// calls wait on a copy of the timeout and copy it back when they return. The message pipe send
// takes the caller's variable itself, and the notify writes the time left to it, so its callback
// sees a value below the one passed and above 1 s. The multiple events wait copies its timeout the
// same way as the object waits, so its callback sees the one passed.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define T_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define TIMEOUT_US 2000000
#define ONE_SECOND_US 1000000
#define TIMER_INTERVAL_US 20000
#define PIPE_BUFFER 4096

typedef enum Kind { RWLOCK_READ, PIPE_SEND, TIMER, LW_COND, SIGNAL, END, MULTI, KIND_COUNT } Kind;

static const char *const kind_names[] = {
    "read lock", "message pipe send", "timer", "lightweight condition", "thread signal", "thread end",
    "multiple events",
};

static Kind kind;
// The object T waits on, the callback, the gate of the thread end and multiple events cases, and
// the helper of the thread end case
static SceUID object, callback, gate, helper, t_uid;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static SceUInt timeout_var;
static SceUInt64 timer_interval = TIMER_INTERVAL_US;
static char pipe_fill[PIPE_BUFFER];
static char pipe_drain[4];
static volatile int callback_ready, callback_ran, wait_result;
static volatile SceUInt callback_saw;

// Records the timeout variable's value
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_saw = *(volatile SceUInt *)&timeout_var;
    callback_ran = 1;
    return 0;
}

// Waits for the gate, then ends
static int helper_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// Creates its callback, then waits with callbacks for the wait of the case
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 bits;
    SceUInt64 user_data;
    SceUInt tmo = TEST_WAIT_US;
    WaitEvent events = { object, EVENT_BIT_A };
    ResultEvent results;
    char buf[4] = { 'a', 'b', 'c', 'd' };
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    timeout_var = TIMEOUT_US;
    callback_ready = 1;
    if (kind == MULTI)
        sceKernelWaitSema(gate, 1, &tmo);
    switch (kind) {
    case RWLOCK_READ:
        wait_result = sceKernelLockReadRWLockCB(object, &timeout_var);
        if (wait_result == 0)
            sceKernelUnlockReadRWLock(object);
        break;
    case PIPE_SEND:
        wait_result = sceKernelSendMsgPipeCB(object, buf, sizeof(buf), MSG_PIPE_MODE_FULL, NULL, &timeout_var);
        break;
    case TIMER: wait_result = sceKernelWaitEventCB(object, EVENT_TIMER, &bits, &user_data, &timeout_var); break;
    case LW_COND:
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        wait_result = sceKernelWaitLwCondCB(&lw_cond, &timeout_var);
        sceKernelUnlockLwMutex(&lw_mutex, 1);
        break;
    case SIGNAL: wait_result = sceKernelWaitSignalCB(0, 0, &timeout_var); break;
    case END: wait_result = sceKernelWaitThreadEndCB(helper, NULL, &timeout_var); break;
    case MULTI: wait_result = sceKernelWaitMultipleEventsCB(&events, 1, EVENT_WAIT_MODE_OR, &results, &timeout_var); break;
    case KIND_COUNT: break;
    }
    sceKernelDeleteCallback(callback);
    return 0;
}

// Returns whether thread t has the wait type, with callbacks
static int type_waits(SceUID t, SceUInt32 type) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(t, &info) == 0 && info.waitType == type;
}

// Returns whether T is in the wait of the case, with callbacks
static int t_waits(SceUID t) {
    switch (kind) {
    case RWLOCK_READ: return test_in_wait(t, WAIT_TYPE_RW_LOCK | WAIT_TYPE_CB, object);
    case PIPE_SEND: return test_in_wait(t, WAIT_TYPE_MSG_PIPE | WAIT_TYPE_CB, object);
    case TIMER: return test_in_wait(t, WAIT_TYPE_EVENT | WAIT_TYPE_CB, object);
    case LW_COND: return type_waits(t, WAIT_TYPE_LW_COND | WAIT_TYPE_CB);
    case SIGNAL: return type_waits(t, WAIT_TYPE_SIGNAL | WAIT_TYPE_CB);
    case END: return type_waits(t, WAIT_TYPE_THREAD_END | WAIT_TYPE_CB);
    case MULTI: return type_waits(t, WAIT_TYPE_MP_EVENTS | WAIT_TYPE_CB);
    case KIND_COUNT: break;
    }
    return 0;
}

// Creates what the wait of the case needs, and takes what T waits for where it has to be held
static void setup(Kind k) {
    SceUInt tmo = TEST_WAIT_US;
    switch (k) {
    case RWLOCK_READ:
        object = sceKernelCreateRWLock("rw", 0, NULL);
        sceKernelLockWriteRWLock(object, NULL);
        break;
    case PIPE_SEND:
        object = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, PIPE_BUFFER, NULL);
        sceKernelSendMsgPipe(object, pipe_fill, PIPE_BUFFER, MSG_PIPE_MODE_FULL, NULL, &tmo);
        break;
    case TIMER:
        object = sceKernelCreateTimer("timer", EVENT_ATTR_MANUAL_RESET, NULL);
        sceKernelSetTimerEvent(object, TIMER_TYPE_SET_EVENT, &timer_interval, 0);
        break;
    case LW_COND:
        sceKernelCreateLwMutex(&lw_mutex, "lw", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "lw cond", 0, &lw_mutex, NULL);
        break;
    case SIGNAL: break;
    case END:
        gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
        helper = sceKernelCreateThread("helper", helper_entry, T_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(helper, 0, NULL);
        break;
    case MULTI:
        gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
        object = sceKernelCreateSimpleEvent("event", 0, 0, NULL);
        break;
    case KIND_COUNT: break;
    }
}

// Gives T what it waits for
static void release(Kind k) {
    SceUInt tmo = TEST_WAIT_US;
    switch (k) {
    case RWLOCK_READ: sceKernelUnlockWriteRWLock(object); break;
    case PIPE_SEND: sceKernelReceiveMsgPipe(object, pipe_drain, sizeof(pipe_drain), MSG_PIPE_MODE_FULL, NULL, &tmo); break;
    case TIMER: sceKernelStartTimer(object); break;
    case LW_COND: sceKernelSignalLwCond(&lw_cond); break;
    case SIGNAL: sceKernelSendSignal(t_uid); break;
    case END: sceKernelSignalSema(gate, 1); break;
    case MULTI: sceKernelSetEvent(object, EVENT_BIT_A, 0); break;
    case KIND_COUNT: break;
    }
}

// Deletes what setup created, and the helper of the thread end case
static void cleanup(Kind k) {
    switch (k) {
    case RWLOCK_READ: sceKernelDeleteRWLock(object); break;
    case PIPE_SEND: sceKernelDeleteMsgPipe(object); break;
    case TIMER: sceKernelDeleteTimer(object); break;
    case LW_COND:
        sceKernelDeleteLwCond(&lw_cond);
        sceKernelDeleteLwMutex(&lw_mutex);
        break;
    case SIGNAL: break;
    case END:
        test_join_thread(helper);
        sceKernelDeleteSema(gate);
        break;
    case MULTI:
        sceKernelDeleteSimpleEvent(object);
        sceKernelDeleteSema(gate);
        break;
    case KIND_COUNT: break;
    }
}

static void run_case(Kind k) {
    kind = k;
    wait_result = NOT_DONE;
    callback_ready = callback_ran = 0;
    callback_saw = NOT_DONE;
    timeout_var = NOT_DONE;
    setup(k);

    const SceUID t = sceKernelCreateThread("t", t_entry, T_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    t_uid = t;
    sceKernelStartThread(t, 0, NULL);
    if (k == MULTI) {
        // T waits at the gate, so the callback is pending when T makes its wait
        TEST_WAIT_FOR(callback_ready);
        sceKernelNotifyCallback(callback, 1);
        sceKernelSignalSema(gate, 1);
    } else {
        // T starts when the main thread blocks here
        TEST_WAIT_FOR(callback_ready && t_waits(t));
        sceKernelNotifyCallback(callback, 1);
    }
    TEST_WAIT_FOR(callback_ran);
    // T leaves its wait for the callback, so it has to be back before it is released
    TEST_WAIT_FOR(t_waits(t));
    const SceUInt saw = callback_saw;
    const int waits = t_waits(t);

    release(k);
    test_join_thread(t);
    const SceUInt left = timeout_var;
    cleanup(k);

    const char *name = kind_names[k];
    PRINTF(waits, "%s: T waits with callbacks before it is released", name);
    PRINTF(wait_result, "%s: T's wait", name);
    PRINTF(saw == TIMEOUT_US, "%s: the timeout the callback saw is the one passed", name);
    PRINTF(saw > ONE_SECOND_US, "%s: the timeout the callback saw is above 1 s", name);
    PRINTF(left < TIMEOUT_US, "%s: the time left after the call is below the timeout passed", name);
    PRINTF(left > ONE_SECOND_US, "%s: the time left after the call is above 1 s", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    for (int k = 0; k < KIND_COUNT; k++)
        run_case((Kind)k);

    test_exit();
    return 0;
}
