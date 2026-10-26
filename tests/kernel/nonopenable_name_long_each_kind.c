// Creating each kind with a 32 character name and without the openable attribute. The kinds are a
// semaphore, event flag, mutex, condition variable (made on a mutex), reader/writer lock, message
// pipe, timer and simple event. The cases, for each kind:
// - whether the object was created
// - for a created one, whether its info name is the first 31 characters of the name
// Each object made is deleted before the next kind. The model open covers the semaphore's 32
// character case only, and this test extends it to the other kinds.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// 32 characters, one past the longest name
#define NAME_32 "abcdefghijklmnopqrstuvwxyz012345"
// The name an info call should show for NAME_32, which is its first 31 characters
#define NAME_31 "abcdefghijklmnopqrstuvwxyz01234"

enum Kind { SEMA, EVENT_FLAG, MUTEX, COND, RWLOCK, MSG_PIPE, SIMPLE_EVENT, TIMER, KIND_COUNT };
static const char *const kind_names[] = {
    "semaphore",          "event flag",   "mutex",        "condition variable",
    "reader/writer lock", "message pipe", "simple event", "timer",
};

// The mutex a condition variable is made on
static SceUID cond_mutex;

// What the info calls fill, with the fields they share first
typedef union Info {
    struct {
        SceSize size;
        SceUID id;
        char name[32];
    } head;
    SceKernelSemaInfo sema;
    SceKernelEventFlagInfo flag;
    MutexInfo mutex;
    SceKernelCondInfo cond;
    SceKernelRWLockInfo rwlock;
    SceKernelMppInfo pipe;
    EventInfo event;
    TimerInfo timer;
} Info;

static SceUID create(int kind, const char *name) {
    switch (kind) {
    case SEMA: return sceKernelCreateSema(name, 0, 0, 1, NULL);
    case EVENT_FLAG: return sceKernelCreateEventFlag(name, 0, 0, NULL);
    case MUTEX: return sceKernelCreateMutex(name, 0, 0, NULL);
    case COND: return sceKernelCreateCond(name, 0, cond_mutex, NULL);
    case RWLOCK: return sceKernelCreateRWLock(name, 0, NULL);
    case MSG_PIPE: return sceKernelCreateMsgPipe(name, MSG_PIPE_TYPE_USER, 0, 16, NULL);
    case SIMPLE_EVENT: return sceKernelCreateSimpleEvent(name, 0, 0, NULL);
    case TIMER: return sceKernelCreateTimer(name, 0, NULL);
    }
    return -1;
}

static int delete_object(int kind, SceUID id) {
    switch (kind) {
    case SEMA: return sceKernelDeleteSema(id);
    case EVENT_FLAG: return sceKernelDeleteEventFlag(id);
    case MUTEX: return sceKernelDeleteMutex(id);
    case COND: return sceKernelDeleteCond(id);
    case RWLOCK: return sceKernelDeleteRWLock(id);
    case MSG_PIPE: return sceKernelDeleteMsgPipe(id);
    case SIMPLE_EVENT: return sceKernelDeleteSimpleEvent(id);
    case TIMER: return sceKernelDeleteTimer(id);
    }
    return -1;
}

// Fills info with the kind's info call through id, and returns its result
static int get_info(int kind, SceUID id, Info *info) {
    sceClibMemset(info, 0, sizeof(*info));
    switch (kind) {
    case SEMA: info->sema.size = sizeof(info->sema); return sceKernelGetSemaInfo(id, &info->sema);
    case EVENT_FLAG:
        info->flag.size = sizeof(info->flag);
        return sceKernelGetEventFlagInfo(id, &info->flag);
    case MUTEX:
        info->mutex.size = sizeof(info->mutex);
        return sceKernelGetMutexInfo(id, (SceKernelMutexInfo *)&info->mutex);
    case COND: info->cond.size = sizeof(info->cond); return sceKernelGetCondInfo(id, &info->cond);
    case RWLOCK:
        info->rwlock.size = sizeof(info->rwlock);
        return sceKernelGetRWLockInfo(id, &info->rwlock);
    case MSG_PIPE:
        info->pipe.size = sizeof(info->pipe);
        return sceKernelGetMsgPipeInfo(id, &info->pipe);
    case SIMPLE_EVENT:
        info->event.size = sizeof(info->event);
        return sceKernelGetEventInfo(id, &info->event);
    case TIMER:
        info->timer.size = sizeof(info->timer);
        return sceKernelGetTimerInfo(id, &info->timer);
    }
    return -1;
}

static void run(int kind) {
    const char *name = kind_names[kind];
    const SceUID id = create(kind, NAME_32);
    PRINTF(test_created(id), "%s: create with a 32 character name and no openable attribute", name);
    if (id < 0)
        return;

    Info info;
    const int result = get_info(kind, id, &info);
    // A failed info call leaves the name empty, so it doesn't match
    PRINTF(result == 0 && sceClibStrncmp(info.head.name, NAME_31, sizeof(info.head.name)) == 0,
           "%s: its info name is the first 31 characters", name);
    delete_object(kind, id);
}

int main(void) {
    cond_mutex = sceKernelCreateMutex("cond mutex", 0, 0, NULL);

    for (int kind = 0; kind < KIND_COUNT; kind++)
        run(kind);

    sceKernelDeleteMutex(cond_mutex);
    test_exit();
    return 0;
}
