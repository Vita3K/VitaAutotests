// The sceKernelGet*Info calls report how each object was created and what state it is in
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define WAIT_US 300000

static SceUID sema, flag, mutex, pipe;

static int sema_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = WAIT_US;
    sceKernelWaitSema(sema, 4, &timeout);
    return 0;
}

static int flag_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = WAIT_US;
    unsigned int bits;
    sceKernelWaitEventFlag(flag, 0x10, EVF_WAITMODE_AND, &bits, &timeout);
    return 0;
}

static int mutex_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = WAIT_US;
    sceKernelLockMutex(mutex, 1, &timeout);
    return 0;
}

static int pipe_receiver(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = WAIT_US;
    char buf[64];
    sceKernelReceiveMsgPipe(pipe, buf, sizeof(buf), MSG_PIPE_MODE_FULL, NULL, &timeout);
    return 0;
}

static int callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    return 0;
}

int main(void) {
    const SceUID self = sceKernelGetThreadId();

    sema = sceKernelCreateSema("sema", 0, 1, 5, NULL);
    sceKernelSignalSema(sema, 2);
    SceUID waiter = test_start_thread(sema_waiter);
    test_wait_until_waiting(waiter);
    SceKernelSemaInfo sema_info = { .size = sizeof(sema_info) };
    PRINT("semaphore info", sceKernelGetSemaInfo(sema, &sema_info));
    test_join_thread(waiter);
    PRINT("semaphore UID", sema_info.semaId == sema);
    PRINT("semaphore name", sceClibStrcmp(sema_info.name, "sema") == 0);
    PRINT("semaphore initial count", sema_info.initCount);
    PRINT("semaphore count", sema_info.currentCount);
    PRINT("semaphore maximum", sema_info.maxCount);
    PRINT("semaphore waiting threads", sema_info.numWaitThreads);

    flag = sceKernelCreateEventFlag("flag", EVF_ATTR_MULTI, 0x3, NULL);
    sceKernelSetEventFlag(flag, 0x4);
    waiter = test_start_thread(flag_waiter);
    test_wait_until_waiting(waiter);
    SceKernelEventFlagInfo flag_info = { .size = sizeof(flag_info) };
    PRINT("event flag info", sceKernelGetEventFlagInfo(flag, &flag_info));
    test_join_thread(waiter);
    PRINT("event flag UID", flag_info.evfId == flag);
    PRINT("event flag name", sceClibStrcmp(flag_info.name, "flag") == 0);
    PRINT("event flag attributes", flag_info.attr);
    PRINT("event flag initial pattern", flag_info.initPattern);
    PRINT("event flag pattern", flag_info.currentPattern);
    PRINT("event flag waiting threads", flag_info.numWaitThreads);

    mutex = sceKernelCreateMutex("mutex", MUTEX_ATTR_RECURSIVE, 1, NULL);
    sceKernelLockMutex(mutex, 1, NULL);
    waiter = test_start_thread(mutex_waiter);
    test_wait_until_waiting(waiter);
    MutexInfo mutex_info = { .size = sizeof(mutex_info) };
    PRINT("mutex info", sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&mutex_info));
    sceKernelUnlockMutex(mutex, 2);
    test_join_thread(waiter);
    PRINT("mutex UID", mutex_info.mutexId == mutex);
    PRINT("mutex name", sceClibStrcmp(mutex_info.name, "mutex") == 0);
    PRINT("mutex attributes", mutex_info.attr);
    PRINT("mutex initial count", mutex_info.initCount);
    PRINT("mutex count", mutex_info.currentCount);
    PRINT("mutex owner", mutex_info.currentOwnerId == self);
    PRINT("mutex waiting threads", mutex_info.numWaitThreads);

    const SceUID cond_mutex = sceKernelCreateMutex("cond_mutex", 0, 0, NULL);
    const SceUID cond = sceKernelCreateCond("cond", 0, cond_mutex, NULL);
    SceKernelCondInfo cond_info = { .size = sizeof(cond_info) };
    PRINT("condition variable info", sceKernelGetCondInfo(cond, &cond_info));
    PRINT("condition variable UID", cond_info.condId == cond);
    PRINT("condition variable name", sceClibStrcmp(cond_info.name, "cond") == 0);
    PRINT("condition variable mutex", cond_info.mutexId == cond_mutex);
    PRINT("condition variable waiting threads", cond_info.numWaitThreads);

    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 256, NULL);
    sceKernelSendMsgPipe(pipe, "abcdefgh", 8, MSG_PIPE_MODE_FULL, NULL, NULL);
    SceKernelMppInfo pipe_info = { .size = sizeof(pipe_info) };
    PRINT("message pipe info", sceKernelGetMsgPipeInfo(pipe, &pipe_info));
    PRINT("message pipe UID", pipe_info.mppId == pipe);
    PRINT("message pipe name", sceClibStrcmp(pipe_info.name, "pipe") == 0);
    PRINT("message pipe buffer size", pipe_info.bufSize);
    PRINT("message pipe free size", pipe_info.freeSize);
    char buf[8];
    SceSize size;
    sceKernelTryReceiveMsgPipe(pipe, buf, 8, MSG_PIPE_MODE_FULL, &size);
    waiter = test_start_thread(pipe_receiver);
    test_wait_until_waiting(waiter);
    sceKernelGetMsgPipeInfo(pipe, &pipe_info);
    test_join_thread(waiter);
    PRINT("message pipe waiting receivers", pipe_info.numReceiveWaitThreads);
    PRINT("message pipe waiting senders", pipe_info.numSendWaitThreads);

    const SceUID rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    sceKernelLockWriteRWLock(rwlock, NULL);
    SceKernelRWLockInfo rwlock_info = { .size = sizeof(rwlock_info) };
    PRINT("reader/writer lock info", sceKernelGetRWLockInfo(rwlock, &rwlock_info));
    sceKernelUnlockWriteRWLock(rwlock);
    PRINT("reader/writer lock UID", rwlock_info.rwLockId == rwlock);
    PRINT("reader/writer lock name", sceClibStrcmp(rwlock_info.name, "rwlock") == 0);
    PRINT("reader/writer lock count", rwlock_info.lockCount);
    PRINT("reader/writer lock writer", rwlock_info.writeOwnerId == self);

    int common = 0;
    const SceUID cb = sceKernelCreateCallback("callback", 0, callback, &common);
    sceKernelNotifyCallback(cb, 0x77);
    CallbackInfo cb_info = { .size = sizeof(cb_info) };
    PRINT("callback info", sceKernelGetCallbackInfo(cb, (SceKernelCallbackInfo *)&cb_info));
    sceKernelCancelCallback(cb);
    PRINT("callback UID", cb_info.callbackId == cb);
    PRINT("callback name", sceClibStrcmp(cb_info.name, "callback") == 0);
    PRINT("callback thread", cb_info.threadId == self);
    PRINT("callback function", cb_info.callback == callback);
    PRINT("callback common argument", cb_info.common == &common);
    PRINT("callback notifications", cb_info.notifyCount);
    PRINT("callback argument", cb_info.notifyArg);

    test_exit();
    return 0;
}
