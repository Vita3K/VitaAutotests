// Which attribute bits each kind of object takes at creation, one bit at a time, and the
// attributes system modules create mutexes with
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MSG_PIPE_TYPE_USER 0x40

static SceUID cond_mutex;
static SceKernelLwMutexWork cond_lw_mutex;

static int callback(int notify_id, int notify_count, int notify_arg, void *common) {
    (void)notify_id;
    (void)notify_count;
    (void)notify_arg;
    (void)common;
    return 0;
}

static int create_mutex(SceUInt32 attr) {
    const SceUID id = sceKernelCreateMutex("attr", attr, 0, NULL);
    sceKernelDeleteMutex(id);
    return test_created(id);
}

static int create_lw_mutex(SceUInt32 attr) {
    SceKernelLwMutexWork work;
    const int ret = sceKernelCreateLwMutex(&work, "attr", attr, 0, NULL);
    if (ret >= 0)
        sceKernelDeleteLwMutex(&work);
    return ret;
}

static int create_sema(SceUInt32 attr) {
    const SceUID id = sceKernelCreateSema("attr", attr, 0, 1, NULL);
    sceKernelDeleteSema(id);
    return test_created(id);
}

static int create_event_flag(SceUInt32 attr) {
    const SceUID id = sceKernelCreateEventFlag("attr", attr, 0, NULL);
    sceKernelDeleteEventFlag(id);
    return test_created(id);
}

static int create_rwlock(SceUInt32 attr) {
    const SceUID id = sceKernelCreateRWLock("attr", attr, NULL);
    sceKernelDeleteRWLock(id);
    return test_created(id);
}

static int create_simple_event(SceUInt32 attr) {
    const SceUID id = sceKernelCreateSimpleEvent("attr", attr, 0, NULL);
    sceKernelDeleteSimpleEvent(id);
    return test_created(id);
}

static int create_msg_pipe(SceUInt32 attr) {
    const SceUID id = sceKernelCreateMsgPipe("attr", MSG_PIPE_TYPE_USER, attr, 0x1000, NULL);
    sceKernelDeleteMsgPipe(id);
    return test_created(id);
}

static int create_cond(SceUInt32 attr) {
    const SceUID id = sceKernelCreateCond("attr", attr, cond_mutex, NULL);
    sceKernelDeleteCond(id);
    return test_created(id);
}

static int create_lw_cond(SceUInt32 attr) {
    SceKernelLwCondWork work;
    const int ret = sceKernelCreateLwCond(&work, "attr", attr, &cond_lw_mutex, NULL);
    if (ret >= 0)
        sceKernelDeleteLwCond(&work);
    return ret;
}

static int create_timer(SceUInt32 attr) {
    const SceUID id = sceKernelCreateTimer("attr", attr, NULL);
    sceKernelDeleteTimer(id);
    return test_created(id);
}

static int create_callback(SceUInt32 attr) {
    const SceUID id = sceKernelCreateCallback("attr", attr, callback, NULL);
    sceKernelDeleteCallback(id);
    return test_created(id);
}

static const struct {
    const char *name;
    int (*create)(SceUInt32 attr);
} kinds[] = {
    { "mutex", create_mutex },
    { "lightweight mutex", create_lw_mutex },
    { "semaphore", create_sema },
    { "event flag", create_event_flag },
    { "reader/writer lock", create_rwlock },
    { "simple event", create_simple_event },
    { "message pipe", create_msg_pipe },
    { "condition variable", create_cond },
    { "lightweight condition variable", create_lw_cond },
    { "timer", create_timer },
    { "callback", create_callback },
};

int main(void) {
    // SceSysmodule creates its mutex with 0x8000
    PRINT("mutex with attribute 0x8000", create_mutex(0x8000));
    PRINT("mutex with attributes 0x8000 and priority", create_mutex(0xA000));
    PRINT("mutex with attributes 0x8000, openable and recursive", create_mutex(0x8082));

    cond_mutex = sceKernelCreateMutex("cond mutex", 0, 0, NULL);
    sceKernelCreateLwMutex(&cond_lw_mutex, "cond lw mutex", 0, 0, NULL);
    for (unsigned int i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        PRINTF(kinds[i].create(0), "%s with no attributes", kinds[i].name);
        for (int bit = 0; bit < 32; bit++)
            PRINTF(kinds[i].create(1u << bit), "%s with attribute 0x%X", kinds[i].name, 1u << bit);
    }
    sceKernelDeleteLwMutex(&cond_lw_mutex);
    sceKernelDeleteMutex(cond_mutex);

    test_exit();
    return 0;
}
