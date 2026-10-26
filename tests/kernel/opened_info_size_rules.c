// Which sizes the info calls of the openable kinds take, through the UID a create gives and through
// the UID an open of its name gives. For a semaphore, event flag, mutex, reader/writer lock,
// message pipe, timer and simple event, each opened once, the calls take the sizes 0, 4, the struct
// size and the struct size plus 4. Each call fills two buffers with room behind them, one prefilled
// with 0xCC and one with 0x33, so a byte the call writes differs from its marker whatever its
// value. The cases:
// - the result of the call through the creating UID, and through the opened UID
// - how many bytes the call wrote through the creating UID, counted up to the last byte it changed
// - whether the two UIDs agree: the same results, and the same bytes in both buffers except the UID
//   field, which holds the UID the call was made with
// The model info_sizes covers the semaphore, event flag, mutex and reader/writer lock through the
// creating UID only. This test adds the opened UIDs and the message pipe, timer and simple event.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NAME "Opened"
#define BUF_SIZE 0x100
// The UID field of an info struct, after its size
#define UID_FIELD_START 4
#define UID_FIELD_END 8

enum Kind { SEMA, EVENT_FLAG, MUTEX, RWLOCK, MSG_PIPE, SIMPLE_EVENT, TIMER, KIND_COUNT };
static const char *const kind_names[] = {
    "semaphore",    "event flag",   "mutex", "reader/writer lock",
    "message pipe", "simple event", "timer",
};

// The struct each kind's info call fills, as its size
static const SceSize struct_sizes[] = {
    sizeof(SceKernelSemaInfo),   sizeof(SceKernelEventFlagInfo), sizeof(MutexInfo),
    sizeof(SceKernelRWLockInfo), sizeof(SceKernelMppInfo),       sizeof(EventInfo),
    sizeof(TimerInfo),
};

typedef int (*InfoFn)(SceUID id, void *info);
static const InfoFn info_fns[] = {
    (InfoFn)sceKernelGetSemaInfo,    (InfoFn)sceKernelGetEventFlagInfo,
    (InfoFn)sceKernelGetMutexInfo,   (InfoFn)sceKernelGetRWLockInfo,
    (InfoFn)sceKernelGetMsgPipeInfo, (InfoFn)sceKernelGetEventInfo,
    (InfoFn)sceKernelGetTimerInfo,
};

static SceUID create(int kind) {
    const SceUInt32 attr = SCE_KERNEL_ATTR_OPENABLE;
    switch (kind) {
    case SEMA: return sceKernelCreateSema(NAME, attr, 0, 16, NULL);
    case EVENT_FLAG: return sceKernelCreateEventFlag(NAME, attr, 0, NULL);
    case MUTEX: return sceKernelCreateMutex(NAME, attr, 0, NULL);
    case RWLOCK: return sceKernelCreateRWLock(NAME, attr, NULL);
    case MSG_PIPE: return sceKernelCreateMsgPipe(NAME, MSG_PIPE_TYPE_USER, attr, 4096, NULL);
    case SIMPLE_EVENT: return sceKernelCreateSimpleEvent(NAME, attr, 0, NULL);
    case TIMER: return sceKernelCreateTimer(NAME, attr, NULL);
    }
    return -1;
}

static SceUID open_object(int kind) {
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

// One info call's result and the two buffers it wrote into
typedef struct Probe {
    int ret;
    int written;
    unsigned char first[BUF_SIZE];
    unsigned char second[BUF_SIZE];
} Probe;

// Calls fn on id with size as the size field, once into each buffer, and counts the bytes written
// past the size field
static void probe(InfoFn fn, SceUID id, SceSize size, Probe *p) {
    sceClibMemset(p->first, 0xCC, sizeof(p->first));
    sceClibMemset(p->second, 0x33, sizeof(p->second));
    sceClibMemcpy(p->first, &size, sizeof(size));
    sceClibMemcpy(p->second, &size, sizeof(size));
    p->ret = fn(id, p->first);
    fn(id, p->second);
    p->written = 0;
    for (int i = sizeof(SceSize); i < BUF_SIZE; i++)
        if (p->first[i] != 0xCC || p->second[i] != 0x33)
            p->written = i + 1;
}

// Whether two probes agree on the result and on every byte but the UID field
static int agree(const Probe *a, const Probe *b) {
    if (a->ret != b->ret)
        return 0;
    for (int i = 0; i < BUF_SIZE; i++) {
        if (i >= UID_FIELD_START && i < UID_FIELD_END)
            continue;
        if (a->first[i] != b->first[i] || a->second[i] != b->second[i])
            return 0;
    }
    return 1;
}

static void run(int kind) {
    static Probe created_probe, opened_probe;
    const char *name = kind_names[kind];
    const SceUID made = create(kind);
    PRINTF(test_created(made), "%s: create an openable object", name);
    const SceUID opened = open_object(kind);
    PRINTF(test_created(opened), "%s: open it by its name", name);

    const SceSize sizes[] = {0, 4, struct_sizes[kind], struct_sizes[kind] + 4};
    for (unsigned int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        const unsigned int size = sizes[i];
        probe(info_fns[kind], made, sizes[i], &created_probe);
        probe(info_fns[kind], opened, sizes[i], &opened_probe);
        PRINTF(created_probe.ret, "%s with a size of 0x%X through the creating UID", name, size);
        PRINTF(opened_probe.ret, "%s with a size of 0x%X through the opened UID", name, size);
        PRINTF(created_probe.written, "%s bytes written with a size of 0x%X", name, size);
        PRINTF(agree(&created_probe, &opened_probe), "%s with a size of 0x%X: the two UIDs agree",
               name, size);
    }

    if (opened >= 0)
        close_object(kind, opened);
    if (made >= 0)
        delete_object(kind, made);
}

int main(void) {
    for (int kind = 0; kind < KIND_COUNT; kind++)
        run(kind);

    test_exit();
    return 0;
}
