// The race patterns of claim_races for the wait kinds it leaves out. Waiter W runs on core 0 below
// a gate thread that spins there without syscalls, so once W's wait ends it stays ready until the
// gate lets go. The main thread runs on core 1. Each case has fresh objects and one of these races:
// - event flag: a set then a delete, a set then a cancel, the timeout passes then a set
// - mutex: an unlock then a cancel (an unlock then a delete is in claim_races)
// - read lock and write lock: an unlock then a delete, an unlock then a cancel
// - simple event: a set then a delete, a set then a cancel
// - message pipe receive: a send then a delete, a send then a cancel
// - condition: a signal then a delete of the condition, a signal then the timeout passes
// Each prints W's status in between, the call that made the race, W's result, what the object holds
// after W ran (-1 once it is deleted), W's unlock where W takes a lock, and whether W got the
// message where W receives one.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_PRIORITY SCE_KERNEL_HIGHEST_PRIORITY_USER
#define W_PRIORITY 0xA0
#define SPINS 400000000
// A timeout the race should end before it passes, and the time the main thread waits for one that
// should pass
#define LONG_US (2 * TEST_WAIT_US)
#define SHORT_US 200000
#define PAST_US 600000
#define MSG_SIZE 4

typedef enum Kind { EVENT_FLAG, MUTEX, READ_LOCK, WRITE_LOCK, SIMPLE_EVENT, MSG_PIPE, COND } Kind;

static SceUID flag, mutex, rwlock, event, pipe, cond, cond_mutex, gate, w;
static volatile Kind kind;
static volatile int gate_up, gate_open, result, unlock_result, done;
static volatile SceUInt w_timeout;
static unsigned int w_bits;
static char message[MSG_SIZE] = { 1, 2, 3, 4 };
static char got[MSG_SIZE];

static int gate_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    gate_up = 1;
    for (volatile int n = 0; n < SPINS && !gate_open; n++)
        ;
    return 0;
}

static void gate_close(void) {
    gate_up = gate_open = 0;
    gate = sceKernelCreateThread("gate", gate_entry, GATE_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(gate, 0, NULL);
    TEST_WAIT_FOR(gate_up);
}

static void gate_lift(void) {
    gate_open = 1;
    sceKernelWaitThreadEnd(gate, NULL, NULL);
    sceKernelDeleteThread(gate);
}

// Waits for the kind's object, and takes what it waits for when the wait succeeds
static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = w_timeout;
    switch (kind) {
    case EVENT_FLAG: result = sceKernelWaitEventFlag(flag, 0x1, SCE_EVENT_WAITAND, &w_bits, &timeout); break;
    case MUTEX:
        result = sceKernelLockMutex(mutex, 1, &timeout);
        if (result == 0)
            unlock_result = sceKernelUnlockMutex(mutex, 1);
        break;
    case READ_LOCK:
        result = sceKernelLockReadRWLock(rwlock, &timeout);
        if (result == 0)
            unlock_result = sceKernelUnlockReadRWLock(rwlock);
        break;
    case WRITE_LOCK:
        result = sceKernelLockWriteRWLock(rwlock, &timeout);
        if (result == 0)
            unlock_result = sceKernelUnlockWriteRWLock(rwlock);
        break;
    case SIMPLE_EVENT: {
        SceUInt32 pattern;
        SceUInt64 user_data;
        result = sceKernelWaitEvent(event, EVENT_BIT_A, &pattern, &user_data, &timeout);
        break;
    }
    case MSG_PIPE: result = sceKernelReceiveMsgPipe(pipe, got, sizeof(got), MSG_PIPE_MODE_FULL, NULL, &timeout); break;
    case COND:
        sceKernelLockMutex(cond_mutex, 1, NULL);
        result = sceKernelWaitCond(cond, &timeout);
        unlock_result = sceKernelUnlockMutex(cond_mutex, 1);
        break;
    }
    done = 1;
    return 0;
}

// Starts W on the kind's wait with the timeout, and returns once W waits
static void start_w(Kind k, SceUInt timeout) {
    kind = k;
    result = unlock_result = 0xDEAD;
    done = 0;
    w_timeout = timeout;
    w = sceKernelCreateThread("w", w_entry, W_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    test_wait_until_waiting(w);
}

static int status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

static int mutex_count(SceUID id) {
    MutexInfo info = { .size = sizeof(info) };
    if (sceKernelGetMutexInfo(id, (SceKernelMutexInfo *)&info) < 0)
        return -1;
    return info.currentCount;
}

// The state of the object the kind's wait is on, or -1 once it is deleted. A condition has none,
// so this reads the mutex that W locks around its wait.
static int state_of(void) {
    if (kind == EVENT_FLAG) {
        SceKernelEventFlagInfo info = { .size = sizeof(info) };
        if (sceKernelGetEventFlagInfo(flag, &info) < 0)
            return -1;
        return info.currentPattern;
    }
    if (kind == MUTEX)
        return mutex_count(mutex);
    if (kind == READ_LOCK || kind == WRITE_LOCK) {
        SceKernelRWLockInfo info = { .size = sizeof(info) };
        if (sceKernelGetRWLockInfo(rwlock, &info) < 0)
            return -1;
        return info.lockCount;
    }
    if (kind == SIMPLE_EVENT) {
        SceUInt32 pattern = 0xDEAD;
        if (sceKernelGetEventPattern(event, &pattern) < 0)
            return -1;
        return pattern;
    }
    if (kind == MSG_PIPE) {
        SceKernelMppInfo info = { .size = sizeof(info) };
        if (sceKernelGetMsgPipeInfo(pipe, &info) < 0)
            return -1;
        return info.bufSize - info.freeSize;
    }
    return mutex_count(cond_mutex);
}

// Lets the gate go, waits for W to end and prints how it ended
static void end_case(const char *name) {
    gate_lift();
    TEST_WAIT_FOR(done);
    test_join_thread(w);
    PRINTF(result, "%s: W's result", name);
    PRINTF(state_of(), "%s: the state after W ran", name);
    if (kind == MUTEX || kind == READ_LOCK || kind == WRITE_LOCK || kind == COND)
        PRINTF(unlock_result, "%s: W's unlock", name);
    if (kind == MSG_PIPE)
        PRINTF(sceClibMemcmp(got, message, sizeof(message)) == 0, "%s: W got the message", name);
}

static void event_flag_set_then_delete(void) {
    flag = sceKernelCreateEventFlag("flag", 0, 0, NULL);
    start_w(EVENT_FLAG, LONG_US);
    gate_close();
    PRINT("event flag set then delete: the set", sceKernelSetEventFlag(flag, 0x1));
    PRINT("event flag set then delete: W's status after the set", status_of(w));
    PRINT("event flag set then delete: the delete", sceKernelDeleteEventFlag(flag));
    end_case("event flag set then delete");
}

static void event_flag_set_then_cancel(void) {
    flag = sceKernelCreateEventFlag("flag", 0, 0, NULL);
    start_w(EVENT_FLAG, LONG_US);
    gate_close();
    PRINT("event flag set then cancel: the set", sceKernelSetEventFlag(flag, 0x1));
    PRINT("event flag set then cancel: W's status after the set", status_of(w));
    SceUInt32 waiting = 0xDEAD;
    PRINT("event flag set then cancel: the cancel", sceKernelCancelEventFlag(flag, 0, &waiting));
    PRINT("event flag set then cancel: the waiters it counts", waiting);
    end_case("event flag set then cancel");
    sceKernelDeleteEventFlag(flag);
}

static void event_flag_timeout_then_set(void) {
    flag = sceKernelCreateEventFlag("flag", 0, 0, NULL);
    start_w(EVENT_FLAG, SHORT_US);
    gate_close();
    sceKernelDelayThread(PAST_US);
    PRINT("event flag timeout then set: W's status once the timeout passed", status_of(w));
    PRINT("event flag timeout then set: the set", sceKernelSetEventFlag(flag, 0x1));
    end_case("event flag timeout then set");
    sceKernelDeleteEventFlag(flag);
}

static void mutex_unlock_then_cancel(void) {
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    sceKernelLockMutex(mutex, 1, NULL);
    start_w(MUTEX, LONG_US);
    gate_close();
    PRINT("mutex unlock then cancel: the unlock", sceKernelUnlockMutex(mutex, 1));
    PRINT("mutex unlock then cancel: W's status after the unlock", status_of(w));
    int waiting = 0xDEAD;
    PRINT("mutex unlock then cancel: the cancel", sceKernelCancelMutex(mutex, 0, &waiting));
    PRINT("mutex unlock then cancel: the waiters it counts", waiting);
    end_case("mutex unlock then cancel");
    sceKernelDeleteMutex(mutex);
}

static void read_lock_unlock_then_delete(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    sceKernelLockWriteRWLock(rwlock, NULL);
    start_w(READ_LOCK, LONG_US);
    gate_close();
    PRINT("read lock unlock then delete: the unlock", sceKernelUnlockWriteRWLock(rwlock));
    PRINT("read lock unlock then delete: W's status after the unlock", status_of(w));
    PRINT("read lock unlock then delete: the delete", sceKernelDeleteRWLock(rwlock));
    end_case("read lock unlock then delete");
}

static void read_lock_unlock_then_cancel(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    sceKernelLockWriteRWLock(rwlock, NULL);
    start_w(READ_LOCK, LONG_US);
    gate_close();
    PRINT("read lock unlock then cancel: the unlock", sceKernelUnlockWriteRWLock(rwlock));
    PRINT("read lock unlock then cancel: W's status after the unlock", status_of(w));
    SceUInt32 readers = 0xDEAD, writers = 0xDEAD;
    PRINT("read lock unlock then cancel: the cancel", sceKernelCancelRWLock(rwlock, &readers, &writers, 0));
    PRINT("read lock unlock then cancel: the readers it counts", readers);
    PRINT("read lock unlock then cancel: the writers it counts", writers);
    end_case("read lock unlock then cancel");
    sceKernelDeleteRWLock(rwlock);
}

static void write_lock_unlock_then_delete(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    sceKernelLockReadRWLock(rwlock, NULL);
    start_w(WRITE_LOCK, LONG_US);
    gate_close();
    PRINT("write lock unlock then delete: the unlock", sceKernelUnlockReadRWLock(rwlock));
    PRINT("write lock unlock then delete: W's status after the unlock", status_of(w));
    PRINT("write lock unlock then delete: the delete", sceKernelDeleteRWLock(rwlock));
    end_case("write lock unlock then delete");
}

static void write_lock_unlock_then_cancel(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    sceKernelLockReadRWLock(rwlock, NULL);
    start_w(WRITE_LOCK, LONG_US);
    gate_close();
    PRINT("write lock unlock then cancel: the unlock", sceKernelUnlockReadRWLock(rwlock));
    PRINT("write lock unlock then cancel: W's status after the unlock", status_of(w));
    SceUInt32 readers = 0xDEAD, writers = 0xDEAD;
    PRINT("write lock unlock then cancel: the cancel", sceKernelCancelRWLock(rwlock, &readers, &writers, 0));
    PRINT("write lock unlock then cancel: the readers it counts", readers);
    PRINT("write lock unlock then cancel: the writers it counts", writers);
    end_case("write lock unlock then cancel");
    sceKernelDeleteRWLock(rwlock);
}

static void simple_event_set_then_delete(void) {
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL);
    start_w(SIMPLE_EVENT, LONG_US);
    gate_close();
    PRINT("simple event set then delete: the set", sceKernelSetEvent(event, EVENT_BIT_A, 0));
    PRINT("simple event set then delete: W's status after the set", status_of(w));
    PRINT("simple event set then delete: the delete", sceKernelDeleteSimpleEvent(event));
    end_case("simple event set then delete");
}

static void simple_event_set_then_cancel(void) {
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL);
    start_w(SIMPLE_EVENT, LONG_US);
    gate_close();
    PRINT("simple event set then cancel: the set", sceKernelSetEvent(event, EVENT_BIT_A, 0));
    PRINT("simple event set then cancel: W's status after the set", status_of(w));
    SceUInt32 waiting = 0xDEAD;
    PRINT("simple event set then cancel: the cancel", sceKernelCancelEvent(event, &waiting));
    PRINT("simple event set then cancel: the waiters it counts", waiting);
    end_case("simple event set then cancel");
    sceKernelDeleteSimpleEvent(event);
}

static void msg_pipe_send_then_delete(void) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 64, NULL);
    start_w(MSG_PIPE, LONG_US);
    gate_close();
    PRINT("message pipe send then delete: the send", sceKernelTrySendMsgPipe(pipe, message, sizeof(message), MSG_PIPE_MODE_FULL, NULL));
    PRINT("message pipe send then delete: W's status after the send", status_of(w));
    PRINT("message pipe send then delete: the delete", sceKernelDeleteMsgPipe(pipe));
    end_case("message pipe send then delete");
}

static void msg_pipe_send_then_cancel(void) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 64, NULL);
    start_w(MSG_PIPE, LONG_US);
    gate_close();
    PRINT("message pipe send then cancel: the send", sceKernelTrySendMsgPipe(pipe, message, sizeof(message), MSG_PIPE_MODE_FULL, NULL));
    PRINT("message pipe send then cancel: W's status after the send", status_of(w));
    int senders = 0xDEAD, receivers = 0xDEAD;
    PRINT("message pipe send then cancel: the cancel", sceKernelCancelMsgPipe(pipe, &senders, &receivers));
    PRINT("message pipe send then cancel: the receivers it counts", receivers);
    end_case("message pipe send then cancel");
    sceKernelDeleteMsgPipe(pipe);
}

static void cond_signal_then_delete(void) {
    cond_mutex = sceKernelCreateMutex("cond mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, cond_mutex, NULL);
    start_w(COND, LONG_US);
    gate_close();
    PRINT("condition signal then delete: the signal", sceKernelSignalCond(cond));
    PRINT("condition signal then delete: W's status after the signal", status_of(w));
    PRINT("condition signal then delete: the delete", sceKernelDeleteCond(cond));
    end_case("condition signal then delete");
    sceKernelDeleteMutex(cond_mutex);
}

static void cond_signal_then_timeout(void) {
    cond_mutex = sceKernelCreateMutex("cond mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, cond_mutex, NULL);
    start_w(COND, SHORT_US);
    gate_close();
    PRINT("condition signal then the timeout passes: the signal", sceKernelSignalCond(cond));
    sceKernelDelayThread(PAST_US);
    PRINT("condition signal then the timeout passes: W's status", status_of(w));
    end_case("condition signal then the timeout passes");
    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(cond_mutex);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    event_flag_set_then_delete();
    event_flag_set_then_cancel();
    event_flag_timeout_then_set();
    mutex_unlock_then_cancel();
    read_lock_unlock_then_delete();
    read_lock_unlock_then_cancel();
    write_lock_unlock_then_delete();
    write_lock_unlock_then_cancel();
    simple_event_set_then_delete();
    simple_event_set_then_cancel();
    msg_pipe_send_then_delete();
    msg_pipe_send_then_cancel();
    cond_signal_then_delete();
    cond_signal_then_timeout();

    test_exit();
    return 0;
}
