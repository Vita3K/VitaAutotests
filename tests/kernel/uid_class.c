// The class sceKernelGetThreadmgrUIDClass reports for each object type. The enum in the vitasdk
// headers has the PSP's classes, so the test compares with the numbers a Vita gives.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static int callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    return 0;
}

int main(void) {
    const SceUID mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);

    PRINT("thread", get_uid_class(sceKernelGetThreadId()));
    PRINT("semaphore", get_uid_class(sceKernelCreateSema("sema", 0, 0, 1, NULL)));
    PRINT("event flag", get_uid_class(sceKernelCreateEventFlag("flag", 0, 0, NULL)));
    PRINT("mutex", get_uid_class(mutex));
    PRINT("condition variable", get_uid_class(sceKernelCreateCond("cond", 0, mutex, NULL)));
    PRINT("timer", get_uid_class(sceKernelCreateTimer("timer", 0, NULL)));
    PRINT("message pipe", get_uid_class(sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 64, NULL)));
    PRINT("callback", get_uid_class(sceKernelCreateCallback("callback", 0, callback, NULL)));
    PRINT("reader/writer lock", get_uid_class(sceKernelCreateRWLock("rwlock", 0, NULL)));
    PRINT("simple event", get_uid_class(sceKernelCreateSimpleEvent("event", 0, 0, NULL)));

    test_exit();
    return 0;
}
