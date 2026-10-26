// What a cancel does through an opened UID that was closed, with counts preset to 0xDEAD, and what
// the cancel through the creating UID does with no waiter. For each kind, the object is made
// OPENABLE, opened by its name, and the opened UID is closed. Then the cancel through the closed UID
// is called, and after it the cancel through the creating UID, which has no waiter. The object is
// deleted last. Each cancel prints its result and the counts it left. The model cancel_unknown_counts
// covers the same cancels on a UID that never existed, on a deleted object and on a UID of another
// kind, and the cancel of a live object with no waiter through its own UID. The kinds are a
// semaphore, a mutex, a reader/writer lock, an event flag, a message pipe, a timer and a simple
// event.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD

enum Kind { SEMA, MUTEX, RWLOCK, FLAG, PIPE, TIMER, EVENT, KINDS };

static const char *const kind_names[KINDS] = {
    "semaphore", "mutex", "reader/writer lock", "event flag", "message pipe", "timer", "simple event",
};

static SceUID create(int kind, const char *name) {
    switch (kind) {
    case SEMA: return sceKernelCreateSema(name, SCE_KERNEL_ATTR_OPENABLE, 0, 3, NULL);
    case MUTEX: return sceKernelCreateMutex(name, SCE_KERNEL_ATTR_OPENABLE, 0, NULL);
    case RWLOCK: return sceKernelCreateRWLock(name, SCE_KERNEL_ATTR_OPENABLE, NULL);
    case FLAG: return sceKernelCreateEventFlag(name, SCE_KERNEL_ATTR_OPENABLE, 0, NULL);
    case PIPE: return sceKernelCreateMsgPipe(name, MSG_PIPE_TYPE_USER, SCE_KERNEL_ATTR_OPENABLE, 4096, NULL);
    case TIMER: return sceKernelCreateTimer(name, SCE_KERNEL_ATTR_OPENABLE, NULL);
    default:
        return sceKernelCreateSimpleEvent(name, SCE_KERNEL_ATTR_OPENABLE | EVENT_ATTR_MANUAL_RESET, 0, NULL);
    }
}

static SceUID open_object(int kind, const char *name) {
    switch (kind) {
    case SEMA: return sceKernelOpenSema(name);
    case MUTEX: return sceKernelOpenMutex(name);
    case RWLOCK: return sceKernelOpenRWLock(name);
    case FLAG: return sceKernelOpenEventFlag(name);
    case PIPE: return sceKernelOpenMsgPipe(name);
    case TIMER: return sceKernelOpenTimer(name);
    default: return sceKernelOpenSimpleEvent(name);
    }
}

static int close_object(int kind, SceUID id) {
    switch (kind) {
    case SEMA: return sceKernelCloseSema(id);
    case MUTEX: return sceKernelCloseMutex(id);
    case RWLOCK: return sceKernelCloseRWLock(id);
    case FLAG: return sceKernelCloseEventFlag(id);
    case PIPE: return sceKernelCloseMsgPipe(id);
    case TIMER: return sceKernelCloseTimer(id);
    default: return sceKernelCloseSimpleEvent(id);
    }
}

static int delete_object(int kind, SceUID id) {
    switch (kind) {
    case SEMA: return sceKernelDeleteSema(id);
    case MUTEX: return sceKernelDeleteMutex(id);
    case RWLOCK: return sceKernelDeleteRWLock(id);
    case FLAG: return sceKernelDeleteEventFlag(id);
    case PIPE: return sceKernelDeleteMsgPipe(id);
    case TIMER: return sceKernelDeleteTimer(id);
    default: return sceKernelDeleteSimpleEvent(id);
    }
}

// Calls the cancel of kind on id with its counts at 0xDEAD, and prints its result and its counts
static void cancel(int kind, SceUID id, const char *where) {
    const char *call = kind_names[kind];
    switch (kind) {
    case SEMA: {
        int waiting = NOT_DONE;
        PRINTF(sceKernelCancelSema(id, 0, &waiting), "%s, %s: the cancel", call, where);
        PRINTF(waiting, "%s, %s: the waiting count", call, where);
        break;
    }
    case MUTEX: {
        int waiting = NOT_DONE;
        PRINTF(sceKernelCancelMutex(id, 0, &waiting), "%s, %s: the cancel", call, where);
        PRINTF(waiting, "%s, %s: the waiting count", call, where);
        break;
    }
    case RWLOCK: {
        SceUInt32 readers = NOT_DONE, writers = NOT_DONE;
        PRINTF(sceKernelCancelRWLock(id, &readers, &writers, 0), "%s, %s: the cancel", call, where);
        PRINTF(readers, "%s, %s: the waiting readers", call, where);
        PRINTF(writers, "%s, %s: the waiting writers", call, where);
        break;
    }
    case FLAG: {
        SceUInt32 waiting = NOT_DONE;
        PRINTF(sceKernelCancelEventFlag(id, 0, &waiting), "%s, %s: the cancel", call, where);
        PRINTF(waiting, "%s, %s: the waiting count", call, where);
        break;
    }
    case PIPE: {
        int senders = NOT_DONE, receivers = NOT_DONE;
        PRINTF(sceKernelCancelMsgPipe(id, &senders, &receivers), "%s, %s: the cancel", call, where);
        PRINTF(senders, "%s, %s: the waiting senders", call, where);
        PRINTF(receivers, "%s, %s: the waiting receivers", call, where);
        break;
    }
    case TIMER: {
        SceUInt32 waiting = NOT_DONE;
        PRINTF(sceKernelCancelTimer(id, &waiting), "%s, %s: the cancel", call, where);
        PRINTF(waiting, "%s, %s: the waiting count", call, where);
        break;
    }
    default: {
        SceUInt32 waiting = NOT_DONE;
        PRINTF(sceKernelCancelEvent(id, &waiting), "%s, %s: the cancel", call, where);
        PRINTF(waiting, "%s, %s: the waiting count", call, where);
        break;
    }
    }
}

int main(void) {
    for (int kind = 0; kind < KINDS; kind++) {
        char name[32];
        sceClibSnprintf(name, sizeof(name), "Closed %d", kind);
        const SceUID creating = create(kind, name);
        const SceUID opened = open_object(kind, name);
        PRINTF(creating < 0 ? creating
               : opened < 0 ? opened
                            : 0,
               "%s: create and open the object", kind_names[kind]);
        PRINTF(close_object(kind, opened), "%s: close the opened UID", kind_names[kind]);
        cancel(kind, opened, "through the closed UID");
        cancel(kind, creating, "through the creating UID, no waiter");
        delete_object(kind, creating);
    }
    test_exit();
    return 0;
}
