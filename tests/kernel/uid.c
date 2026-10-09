// UIDs of one object type don't work with the calls of another, and deleted objects are gone
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    const SceUID sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    const SceUID flag = sceKernelCreateEventFlag("flag", 0, 0, NULL);
    const SceUID rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    const SceUID pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 64, NULL);
    SceSize size;
    char buf[4];

    PRINT("lock a semaphore as a mutex", sceKernelLockMutex(sema, 1, NULL));
    PRINT("signal a mutex as a semaphore", sceKernelSignalSema(mutex, 1));
    PRINT("set a semaphore as an event flag", sceKernelSetEventFlag(sema, 1));
    PRINT("signal an event flag as a condition variable", sceKernelSignalCond(flag));
    PRINT("read lock a message pipe as a reader/writer lock", sceKernelTryLockReadRWLock(pipe));
    PRINT("receive from a reader/writer lock as a message pipe", sceKernelTryReceiveMsgPipe(rwlock, buf, 1, MSG_PIPE_MODE_ASAP, &size));

    sceKernelDeleteSema(sema);
    PRINT("signal a deleted semaphore", sceKernelSignalSema(sema, 1));
    PRINT("delete a semaphore twice", sceKernelDeleteSema(sema));
    PRINT("the class of a deleted object", get_uid_class(sema) < 0);

    const SceUID event = sceKernelCreateSimpleEvent("event", 0, 0, NULL);
    sceKernelDeleteSimpleEvent(event);
    PRINT("set a deleted simple event", sceKernelSetEvent(event, 1, 0) < 0);
    PRINT("the class of a deleted simple event", get_uid_class(event) < 0);

    test_exit();
    return 0;
}
