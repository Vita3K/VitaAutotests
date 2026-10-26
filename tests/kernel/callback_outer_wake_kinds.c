// A thread waits with callbacks, the callback it runs blocks in a wait of its own, and the test
// wakes or frees what the first wait waits for in between. This covers the wait kinds that
// callback_outer_wake and callback_outer_wake_more leave out: a timer event, a message pipe send
// (callback_outer_wake_send has it too), a thread signal, and the cancel and the delete of an event
// flag, a mutex, a rwlock and a simple event. The semaphore cancel and delete are in
// callback_outer_wake. Each case reads what the object reports while the callback waits, makes the
// wake, then ends the callback's wait and prints how both waits ended. The thread stays blocked in
// the callback's wait until the test signals it, so what the test reads in between can't change.
// One cancel or delete per case.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

enum Kind {
    TIMER,
    MSG_SEND,
    SIGNAL,
    FLAG_CANCEL,
    FLAG_DELETE,
    MUTEX_CANCEL,
    RW_CANCEL,
    RW_DELETE,
    SIMPLE_CANCEL,
    SIMPLE_DELETE
};

// One send of this many bytes fills the pipe
#define PIPE_SIZE 0x1000

static SceUID inner, cb, flag, mutex, rwlock, event, timer, pipe;
static volatile int inner_result, outer_result;
static SceUInt outer_left;
static unsigned int outer_bits;
static char message[4] = { 1, 2, 3, 4 };
static char fill[PIPE_SIZE];

// Blocks in a wait on another semaphore, until the test signals it
static int inner_wait_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    SceUInt timeout = 2 * TEST_WAIT_US;
    inner_result = sceKernelWaitSema(inner, 1, &timeout);
    return 0;
}

static int outer_thread(SceSize args, void *argp) {
    (void)args;
    cb = sceKernelCreateCallback("inner", 0, inner_wait_callback, NULL);
    outer_left = 4 * TEST_WAIT_US;
    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    switch (*(const int *)argp) {
    case TIMER: outer_result = sceKernelWaitEventCB(timer, EVENT_TIMER, &pattern, &user_data, &outer_left); break;
    case MSG_SEND:
        outer_result = sceKernelSendMsgPipeCB(pipe, message, sizeof(message), MSG_PIPE_MODE_FULL, NULL, &outer_left);
        break;
    case SIGNAL: outer_result = sceKernelWaitSignalCB(0, 0, &outer_left); break;
    case FLAG_CANCEL:
    case FLAG_DELETE:
        outer_result = sceKernelWaitEventFlagCB(flag, 0x1, SCE_EVENT_WAITAND | SCE_EVENT_WAITCLEAR, &outer_bits, &outer_left);
        break;
    case MUTEX_CANCEL:
        outer_result = sceKernelLockMutexCB(mutex, 1, &outer_left);
        if (outer_result == 0)
            sceKernelUnlockMutex(mutex, 1);
        break;
    case RW_CANCEL:
    case RW_DELETE:
        outer_result = sceKernelLockWriteRWLockCB(rwlock, &outer_left);
        if (outer_result == 0)
            sceKernelUnlockWriteRWLock(rwlock);
        break;
    case SIMPLE_CANCEL:
    case SIMPLE_DELETE: outer_result = sceKernelWaitEventCB(event, EVENT_BIT_A, &pattern, &user_data, &outer_left); break;
    }
    sceKernelDeleteCallback(cb);
    return 0;
}

static int sema_count(SceUID id) {
    SceKernelSemaInfo info = { .size = sizeof(info) };
    sceKernelGetSemaInfo(id, &info);
    return info.currentCount;
}

static EventInfo event_info(SceUID id) {
    EventInfo info = { .size = sizeof(info) };
    sceKernelGetEventInfo(id, &info);
    return info;
}

// Starts a thread waiting in the given way and makes its callback block on the inner semaphore
static SceUID start_in_callback(int kind) {
    inner = sceKernelCreateSema("inner", 0, 0, 1, NULL);
    inner_result = outer_result = 0xDEAD;
    const SceUID thread = test_start_thread_arg(outer_thread, kind);
    test_wait_until_waiting(thread);
    sceKernelNotifyCallback(cb, 1);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_SEMA, inner));
    return thread;
}

// Ends the callback's wait, waits for the thread to end and prints how both waits ended. If the
// wake for the outer wait had ended the callback's wait too, the signal here is left over.
static void finish(const char *name, SceUID thread) {
    sceKernelSignalSema(inner, 1);
    test_join_thread(thread);
    PRINTF(inner_result, "%s: the callback's wait", name);
    PRINTF(sema_count(inner), "%s: the inner semaphore's count at the end", name);
    PRINTF(outer_result, "%s: the outer wait", name);
    PRINTF(outer_left > 0, "%s: the outer wait has time left", name);
    sceKernelDeleteSema(inner);
}

// A timer's event, which the outer wait waits for, comes while the callback waits
static void timer_case(void) {
    timer = sceKernelCreateTimer("timer", EVENT_ATTR_AUTO_RESET, NULL);
    SceUInt64 interval = 100000;
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    const SceUID thread = start_in_callback(TIMER);
    PRINT("timer: its waiters while the callback waits", event_info(timer).numWaitThreads);
    PRINT("timer: start it", sceKernelStartTimer(timer));
    TEST_WAIT_FOR(event_info(timer).eventPattern & EVENT_TIMER);
    const EventInfo info = event_info(timer);
    PRINT("timer: its event after it fired", info.eventPattern & EVENT_TIMER);
    PRINT("timer: its waiters after it fired", info.numWaitThreads);
    finish("timer", thread);
    PRINT("timer: its event at the end", event_info(timer).eventPattern & EVENT_TIMER);
    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

// The pipe is full, and the outer send waits to put its message in
static void msg_send_case(void) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, PIPE_SIZE, NULL);
    PRINT("message pipe send: fill the pipe", sceKernelTrySendMsgPipe(pipe, fill, sizeof(fill), MSG_PIPE_MODE_FULL, NULL));
    const SceUID thread = start_in_callback(MSG_SEND);
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    PRINT("message pipe send: its senders while the callback waits", info.numSendWaitThreads);
    PRINT("message pipe send: empty the pipe", sceKernelTryReceiveMsgPipe(pipe, fill, sizeof(fill), MSG_PIPE_MODE_FULL, NULL));
    sceKernelGetMsgPipeInfo(pipe, &info);
    PRINT("message pipe send: the bytes in it after the receive", info.bufSize - info.freeSize);
    PRINT("message pipe send: its senders after the receive", info.numSendWaitThreads);
    finish("message pipe send", thread);
    char got[4] = { 0 };
    PRINT("message pipe send: receive the message", sceKernelTryReceiveMsgPipe(pipe, got, sizeof(got), MSG_PIPE_MODE_FULL, NULL));
    PRINT("message pipe send: it is the sent message", sceClibMemcmp(got, message, sizeof(message)) == 0);
    sceKernelDeleteMsgPipe(pipe);
}

// A signal to the thread while its callback waits
static void signal_case(void) {
    const SceUID thread = start_in_callback(SIGNAL);
    PRINT("thread signal: send the signal", sceKernelSendSignal(thread));
    finish("thread signal", thread);
}

static void flag_cancel_case(void) {
    flag = sceKernelCreateEventFlag("flag", 0, 0, NULL);
    const SceUID thread = start_in_callback(FLAG_CANCEL);
    SceKernelEventFlagInfo info = { .size = sizeof(info) };
    sceKernelGetEventFlagInfo(flag, &info);
    PRINT("event flag cancel: its waiters while the callback waits", info.numWaitThreads);
    SceUInt32 waiting = 0xDEAD;
    PRINT("event flag cancel: the cancel", sceKernelCancelEventFlag(flag, 0, &waiting));
    PRINT("event flag cancel: the waiters it counts", waiting);
    finish("event flag cancel", thread);
    sceKernelGetEventFlagInfo(flag, &info);
    PRINT("event flag cancel: its bits at the end", info.currentPattern);
    sceKernelDeleteEventFlag(flag);
}

static void flag_delete_case(void) {
    flag = sceKernelCreateEventFlag("flag", 0, 0, NULL);
    const SceUID thread = start_in_callback(FLAG_DELETE);
    PRINT("event flag delete: the delete", sceKernelDeleteEventFlag(flag));
    finish("event flag delete", thread);
}

static void mutex_cancel_case(void) {
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    sceKernelLockMutex(mutex, 1, NULL);
    const SceUID thread = start_in_callback(MUTEX_CANCEL);
    int waiting = 0xDEAD;
    PRINT("mutex cancel: the cancel", sceKernelCancelMutex(mutex, 0, &waiting));
    PRINT("mutex cancel: the waiters it counts", waiting);
    finish("mutex cancel", thread);
    SceKernelMutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, &info);
    PRINT("mutex cancel: its count at the end", info.currentCount);
    sceKernelDeleteMutex(mutex);
}

static void rwlock_cancel_case(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    sceKernelLockWriteRWLock(rwlock, NULL);
    const SceUID thread = start_in_callback(RW_CANCEL);
    SceUInt32 readers = 0xDEAD, writers = 0xDEAD;
    PRINT("rwlock cancel: the cancel", sceKernelCancelRWLock(rwlock, &readers, &writers, 0));
    PRINT("rwlock cancel: the readers it counts", readers);
    PRINT("rwlock cancel: the writers it counts", writers);
    finish("rwlock cancel", thread);
    SceKernelRWLockInfo info = { .size = sizeof(info) };
    sceKernelGetRWLockInfo(rwlock, &info);
    PRINT("rwlock cancel: its lock count at the end", info.lockCount);
    sceKernelDeleteRWLock(rwlock);
}

static void rwlock_delete_case(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    sceKernelLockWriteRWLock(rwlock, NULL);
    const SceUID thread = start_in_callback(RW_DELETE);
    PRINT("rwlock delete: the delete", sceKernelDeleteRWLock(rwlock));
    finish("rwlock delete", thread);
}

static void event_cancel_case(void) {
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL);
    const SceUID thread = start_in_callback(SIMPLE_CANCEL);
    SceUInt32 waiting = 0xDEAD;
    PRINT("simple event cancel: the cancel", sceKernelCancelEvent(event, &waiting));
    PRINT("simple event cancel: the waiters it counts", waiting);
    finish("simple event cancel", thread);
    sceKernelDeleteSimpleEvent(event);
}

static void event_delete_case(void) {
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL);
    const SceUID thread = start_in_callback(SIMPLE_DELETE);
    PRINT("simple event delete: the delete", sceKernelDeleteSimpleEvent(event));
    finish("simple event delete", thread);
}

int main(void) {
    timer_case();
    msg_send_case();
    signal_case();
    flag_cancel_case();
    flag_delete_case();
    mutex_cancel_case();
    rwlock_cancel_case();
    rwlock_delete_case();
    event_cancel_case();
    event_delete_case();

    test_exit();
    return 0;
}
