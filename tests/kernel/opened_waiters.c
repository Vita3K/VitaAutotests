// Waiters on an openable object of each kind with an open call while one of its UIDs goes away.
// Each case makes the object, opens it once by its name, and starts a thread that waits through
// one of the two UIDs. Then the main thread deletes the creating UID or closes the opened one,
// reads right after how many waiters the info call through the UID that is left counts, and
// releases the object through that UID. The test prints whether the UID the waiting thread's info
// reports as the one it waits on is one of the object's two UIDs, the delete or close, the waiters
// it counted, the release, and what the wait returned. The cases:
// - the wait through the opened UID, and the creating UID deleted
// - the wait through the opened UID, and that UID closed
// - the wait through the creating UID, and that UID deleted
// - the wait through the creating UID, and the opened UID closed
// The waits are a semaphore wait, an event flag wait, a mutex lock while the main thread holds the
// mutex, a read lock while it holds the write lock, a message pipe receive, a simple event wait and
// a wait for a timer's event, each with a timeout of 2 seconds. The releases are a signal, a set,
// an unlock, a write unlock, a send, a set, and for the timer an event 1 ms ahead with the timer
// started. Which of the two UIDs the waiting thread reports isn't printed, since that differs
// between consoles.
//
// On a PS Vita TV on firmware 3.65 the waiter outlives the loss of either UID, even the UID it
// waits through, for every kind but the timer's delete. The delete or close works, the info call
// through the UID left still counts the waiter, and the release through it ends the wait with
// success. The waiting thread's info reports one of the object's two UIDs as the one it waits on,
// not always the one it waits through, and which one differs between cases that wait the same way.
// A timer behaves the same when its opened UID is closed, but deleting its creating UID makes the
// opened UID give 0x80028241 (UNKNOWN_TIMER_ID) to the info call and the release, whichever UID the
// waiter waits through, and the wait times out with 0x80028005 (WAIT_TIMEOUT).
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// Each case's own name, since a timer whose creating UID is deleted keeps its name taken
static char object_name[32];
#define NAME object_name

enum Kind { SEMA, EVENT_FLAG, MUTEX, RWLOCK, MSG_PIPE, SIMPLE_EVENT, TIMER, KIND_COUNT };
static const char *const kind_names[] = {
    "semaphore", "event flag", "mutex", "reader/writer lock", "message pipe", "simple event",
    "timer",
};

enum Case { OPENED_DELETE, OPENED_CLOSE, CREATING_DELETE, CREATING_CLOSE, CASE_COUNT };
static const char *const case_names[] = {
    "wait through the opened UID, delete the creating UID",
    "wait through the opened UID, close it",
    "wait through the creating UID, delete it",
    "wait through the creating UID, close the opened UID",
};

static volatile int kind;
static SceUID wait_uid;
static volatile int wait_result;

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    char message[4];
    unsigned int bits;
    SceUInt32 pattern;
    SceUInt64 user_data;
    int result = -1;
    switch (kind) {
    case SEMA: result = sceKernelWaitSema(wait_uid, 1, &timeout); break;
    case EVENT_FLAG:
        result = sceKernelWaitEventFlag(wait_uid, 0x1, SCE_EVENT_WAITOR, &bits, &timeout);
        break;
    case MUTEX: result = sceKernelLockMutex(wait_uid, 1, &timeout); break;
    case RWLOCK: result = sceKernelLockReadRWLock(wait_uid, &timeout); break;
    case MSG_PIPE:
        result = sceKernelReceiveMsgPipe(wait_uid, message, sizeof(message), MSG_PIPE_MODE_FULL,
            NULL, &timeout);
        break;
    case SIMPLE_EVENT:
        result = sceKernelWaitEvent(wait_uid, EVENT_BIT_A, &pattern, &user_data, &timeout);
        break;
    case TIMER:
        result = sceKernelWaitEvent(wait_uid, EVENT_TIMER, &pattern, &user_data, &timeout);
        break;
    }
    wait_result = result;
    return 0;
}

// Creates the object, held by the main thread where it is a lock
static SceUID create(void) {
    const SceUInt32 attr = SCE_KERNEL_ATTR_OPENABLE;
    SceUID id = -1;
    switch (kind) {
    case SEMA: return sceKernelCreateSema(NAME, attr, 0, 16, NULL);
    case EVENT_FLAG: return sceKernelCreateEventFlag(NAME, attr, 0, NULL);
    case MUTEX: return sceKernelCreateMutex(NAME, attr, 1, NULL);
    case RWLOCK:
        id = sceKernelCreateRWLock(NAME, attr, NULL);
        sceKernelLockWriteRWLock(id, NULL);
        return id;
    case MSG_PIPE: return sceKernelCreateMsgPipe(NAME, MSG_PIPE_TYPE_USER, attr, 4096, NULL);
    case SIMPLE_EVENT: return sceKernelCreateSimpleEvent(NAME, attr, 0, NULL);
    case TIMER: return sceKernelCreateTimer(NAME, attr, NULL);
    }
    return id;
}

static SceUID open_object(void) {
    switch (kind) {
    case SEMA: return sceKernelOpenSema(NAME);
    case EVENT_FLAG: return sceKernelOpenEventFlag(NAME);
    case MUTEX: return sceKernelOpenMutex(NAME);
    case RWLOCK: return sceKernelOpenRWLock(NAME);
    case MSG_PIPE: return sceKernelOpenMsgPipe(NAME);
    case SIMPLE_EVENT: return sceKernelOpenSimpleEvent(NAME);
    case TIMER: return sceKernelOpenTimer(NAME);
    }
    return -1;
}

static int close_object(SceUID id) {
    switch (kind) {
    case SEMA: return sceKernelCloseSema(id);
    case EVENT_FLAG: return sceKernelCloseEventFlag(id);
    case MUTEX: return sceKernelCloseMutex(id);
    case RWLOCK: return sceKernelCloseRWLock(id);
    case MSG_PIPE: return sceKernelCloseMsgPipe(id);
    case SIMPLE_EVENT: return sceKernelCloseSimpleEvent(id);
    case TIMER: return sceKernelCloseTimer(id);
    }
    return -1;
}

static int delete_object(SceUID id) {
    switch (kind) {
    case SEMA: return sceKernelDeleteSema(id);
    case EVENT_FLAG: return sceKernelDeleteEventFlag(id);
    case MUTEX: return sceKernelDeleteMutex(id);
    case RWLOCK: return sceKernelDeleteRWLock(id);
    case MSG_PIPE: return sceKernelDeleteMsgPipe(id);
    case SIMPLE_EVENT: return sceKernelDeleteSimpleEvent(id);
    case TIMER: return sceKernelDeleteTimer(id);
    }
    return -1;
}

// Ends the wait through id
static int release(SceUID id) {
    static char message[4];
    SceSize size;
    SceUInt64 interval = 1000;
    int result;
    switch (kind) {
    case SEMA: return sceKernelSignalSema(id, 1);
    case EVENT_FLAG: return sceKernelSetEventFlag(id, 0x1);
    case MUTEX: return sceKernelUnlockMutex(id, 1);
    case RWLOCK: return sceKernelUnlockWriteRWLock(id);
    case MSG_PIPE:
        return sceKernelTrySendMsgPipe(id, message, sizeof(message), MSG_PIPE_MODE_ASAP, &size);
    case SIMPLE_EVENT: return sceKernelSetEvent(id, EVENT_BIT_A, 0);
    case TIMER:
        result = sceKernelSetTimerEvent(id, TIMER_TYPE_SET_EVENT, &interval, 0);
        return result < 0 ? result : sceKernelStartTimer(id);
    }
    return -1;
}

// The waiters the info call through id counts, or its error
static int waiters(SceUID id) {
    SceKernelSemaInfo sema = { .size = sizeof(sema) };
    SceKernelEventFlagInfo flag = { .size = sizeof(flag) };
    MutexInfo mutex = { .size = sizeof(mutex) };
    SceKernelRWLockInfo rwlock = { .size = sizeof(rwlock) };
    SceKernelMppInfo pipe = { .size = sizeof(pipe) };
    EventInfo event = { .size = sizeof(event) };
    TimerInfo timer = { .size = sizeof(timer) };
    int result = -1;
    switch (kind) {
    case SEMA: return (result = sceKernelGetSemaInfo(id, &sema)) < 0 ? result : sema.numWaitThreads;
    case EVENT_FLAG:
        return (result = sceKernelGetEventFlagInfo(id, &flag)) < 0 ? result : flag.numWaitThreads;
    case MUTEX:
        result = sceKernelGetMutexInfo(id, (SceKernelMutexInfo *)&mutex);
        return result < 0 ? result : (int)mutex.numWaitThreads;
    case RWLOCK:
        result = sceKernelGetRWLockInfo(id, &rwlock);
        return result < 0 ? result : (int)(rwlock.numReadWaitThreads + rwlock.numWriteWaitThreads);
    case MSG_PIPE:
        result = sceKernelGetMsgPipeInfo(id, &pipe);
        return result < 0 ? result : pipe.numReceiveWaitThreads;
    case SIMPLE_EVENT:
        result = sceKernelGetEventInfo(id, &event);
        return result < 0 ? result : (int)event.numWaitThreads;
    case TIMER:
        result = sceKernelGetTimerInfo(id, &timer);
        return result < 0 ? result : (int)timer.numWaitThreads;
    }
    return result;
}

static SceUID wait_id(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? info.waitId : -1;
}

static void run(int which) {
    const char *name = kind_names[kind];
    const char *what = case_names[which];
    const int through_opened = which == OPENED_DELETE || which == OPENED_CLOSE;
    const int deletes = which == OPENED_DELETE || which == CREATING_DELETE;
    sceClibSnprintf(object_name, sizeof(object_name), "Waited %d %d", kind, which);
    const SceUID id = create();
    const SceUID opened = open_object();
    PRINTF(id < 0 ? id : opened < 0 ? opened : 0, "%s, %s: create and open the object", name, what);
    wait_uid = through_opened ? opened : id;
    const SceUID other = through_opened ? id : opened;
    const SceUID left = deletes ? opened : id;
    wait_result = 0xDEAD;

    const SceUID thread = test_start_thread(waiter);
    test_wait_until_waiting(thread);
    const SceUID waits_on = wait_id(thread);
    const int removed = deletes ? delete_object(id) : close_object(opened);
    // Read right after, so a wake the delete or close didn't do can't be missed
    const int counted = waiters(left);
    const int released = release(left);
    test_join_thread(thread);
    if (kind == TIMER)
        sceKernelStopTimer(left);

    PRINTF(waits_on == wait_uid || waits_on == other, "%s, %s: the UID it waits on is one of its two",
        name, what);
    PRINTF(removed, "%s, %s: %s", name, what, deletes ? "the delete" : "the close");
    PRINTF(counted, "%s, %s: waiters through the UID left", name, what);
    PRINTF(released, "%s, %s: release through it", name, what);
    PRINTF(wait_result, "%s, %s: the wait", name, what);

    if (!deletes)
        delete_object(id);
    else
        close_object(opened);
}

int main(void) {
    for (kind = 0; kind < KIND_COUNT; kind++)
        for (int which = 0; which < CASE_COUNT; which++)
            run(which);
    test_exit();
    return 0;
}
