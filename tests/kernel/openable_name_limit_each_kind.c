// The name length limits of each openable kind, with the semaphore as the control. For a semaphore,
// event flag, mutex, reader/writer lock, message pipe, timer and simple event, the test makes an
// openable object with a 31 character name, opens it by that name, opens the name with one more
// character, makes an openable object with a 32 character name, and deletes the first object. The
// cases:
// - the result of each of the five calls, in that order
// - for every kind but the semaphore, whether each result matches the semaphore's
// The model open_name_rules checks these limits for the semaphore only, so this test adds the other
// openable kinds.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// 31 characters
#define NAME_31 "abcdefghijklmnopqrstuvwxyz01234"
// One more character
#define NAME_32 NAME_31 "5"

enum Kind { SEMA, EVENT_FLAG, MUTEX, RWLOCK, MSG_PIPE, SIMPLE_EVENT, TIMER, KIND_COUNT };
static const char *const kind_names[] = {
    "semaphore",    "event flag",   "mutex", "reader/writer lock",
    "message pipe", "simple event", "timer",
};

#define RESULT_COUNT 5
static const char *const result_names[RESULT_COUNT] = {
    "create with a 31 character name", "open by it", "open by it and one more character",
    "create with a 32 character name", "delete",
};

// The results of the semaphore, which the other kinds are compared with
static int control[RESULT_COUNT];

static SceUID create(int kind, const char *name) {
    const SceUInt32 attr = SCE_KERNEL_ATTR_OPENABLE;
    switch (kind) {
    case SEMA: return sceKernelCreateSema(name, attr, 0, 16, NULL);
    case EVENT_FLAG: return sceKernelCreateEventFlag(name, attr, 0, NULL);
    case MUTEX: return sceKernelCreateMutex(name, attr, 0, NULL);
    case RWLOCK: return sceKernelCreateRWLock(name, attr, NULL);
    case MSG_PIPE: return sceKernelCreateMsgPipe(name, MSG_PIPE_TYPE_USER, attr, 4096, NULL);
    case SIMPLE_EVENT: return sceKernelCreateSimpleEvent(name, attr, 0, NULL);
    case TIMER: return sceKernelCreateTimer(name, attr, NULL);
    }
    return -1;
}

static SceUID open_object(int kind, const char *name) {
    switch (kind) {
    case SEMA: return sceKernelOpenSema(name);
    case EVENT_FLAG: return sceKernelOpenEventFlag(name);
    case MUTEX: return sceKernelOpenMutex(name);
    case RWLOCK: return sceKernelOpenRWLock(name);
    case MSG_PIPE: return sceKernelOpenMsgPipe(name);
    case SIMPLE_EVENT: return sceKernelOpenSimpleEvent(name);
    case TIMER: return sceKernelOpenTimer(name);
    }
    return -1;
}

static int close_object(int kind, SceUID id) {
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

static int delete_object(int kind, SceUID id) {
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

static void run(int kind) {
    const char *name = kind_names[kind];
    int results[RESULT_COUNT];

    const SceUID id = create(kind, NAME_31);
    results[0] = test_created(id);
    const SceUID opened = open_object(kind, NAME_31);
    results[1] = test_created(opened);
    const SceUID longer = open_object(kind, NAME_32);
    results[2] = test_created(longer);
    const SceUID long_id = create(kind, NAME_32);
    results[3] = test_created(long_id);
    results[4] = delete_object(kind, id);

    // Closes the opened UIDs and deletes the object made with the 32 character name, so the next
    // kind starts clean
    if (opened >= 0)
        close_object(kind, opened);
    if (longer >= 0)
        close_object(kind, longer);
    if (long_id >= 0)
        delete_object(kind, long_id);

    for (int i = 0; i < RESULT_COUNT; i++) {
        PRINTF(results[i], "%s: %s", name, result_names[i]);
        if (kind == SEMA)
            control[i] = results[i];
        else
            PRINTF(results[i] == control[i], "%s: %s matches the semaphore's", name,
                   result_names[i]);
    }
}

int main(void) {
    for (int kind = 0; kind < KIND_COUNT; kind++)
        run(kind);

    test_exit();
    return 0;
}
