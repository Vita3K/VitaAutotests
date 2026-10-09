// sceKernelGetThreadInfo reports what a waiting thread waits on
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// Every wait times out by itself, so the test never needs to end them
#define WAIT_US 300000

enum Kind { DELAY, DELAY_CB, SEMA, SEMA_CB, EVENT_FLAG, MUTEX, RW_LOCK, COND, SIMPLE_EVENT, MSG_PIPE, LW_MUTEX, LW_COND, THREAD_END, KIND_COUNT };

static SceUID sema, flag, mutex, rwlock, cond, event, pipe, sleeper;
static SceKernelLwMutexWork lw_mutex, lw_cond_mutex;
static SceKernelLwCondWork lw_cond;

static int sleep_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelDelayThread(WAIT_US);
    return 0;
}

static int wait_thread(SceSize args, void *argp) {
    (void)args;
    SceUInt timeout = WAIT_US;
    unsigned int bits;
    char buf[4];
    SceUInt32 result;
    SceUInt64 user_data;
    switch (*(const int *)argp) {
    case DELAY: sceKernelDelayThread(WAIT_US); break;
    case DELAY_CB: sceKernelDelayThreadCB(WAIT_US); break;
    case SEMA: sceKernelWaitSema(sema, 1, &timeout); break;
    case SEMA_CB: sceKernelWaitSemaCB(sema, 1, &timeout); break;
    case EVENT_FLAG: sceKernelWaitEventFlag(flag, 1, EVF_WAITMODE_AND, &bits, &timeout); break;
    case MUTEX: sceKernelLockMutex(mutex, 1, &timeout); break;
    case RW_LOCK: sceKernelLockReadRWLock(rwlock, &timeout); break;
    case COND:
        sceKernelLockMutex(mutex, 1, NULL);
        sceKernelWaitCond(cond, &timeout);
        sceKernelUnlockMutex(mutex, 1);
        break;
    case SIMPLE_EVENT: sceKernelWaitEvent(event, 1, &result, &user_data, &timeout); break;
    case MSG_PIPE: sceKernelReceiveMsgPipe(pipe, buf, sizeof(buf), MSG_PIPE_MODE_FULL, NULL, &timeout); break;
    case LW_MUTEX: sceKernelLockLwMutex(&lw_mutex, 1, &timeout); break;
    case LW_COND:
        sceKernelLockLwMutex(&lw_cond_mutex, 1, NULL);
        sceKernelWaitLwCond(&lw_cond, &timeout);
        sceKernelUnlockLwMutex(&lw_cond_mutex, 1);
        break;
    case THREAD_END: sceKernelWaitThreadEnd(sleeper, NULL, &timeout); break;
    }
    return 0;
}

// Starts a thread waiting in the given way, and prints the wait type it reports and whether its
// wait UID is id. With id 0, the UID isn't checked.
static void print_wait(const char *name, int kind, SceUID id) {
    const SceUID thread = test_start_thread_arg(wait_thread, kind);
    test_wait_until_waiting(thread);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    const int result = sceKernelGetThreadInfo(thread, &info);
    test_join_thread(thread);

    char label[64];
    PRINT(name, result);
    sceClibSnprintf(label, sizeof(label), "%s wait type", name);
    PRINT(label, info.waitType);
    if (id) {
        sceClibSnprintf(label, sizeof(label), "%s wait UID is the object", name);
        PRINT(label, info.waitId == id);
    }
}

int main(void) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    flag = sceKernelCreateEventFlag("flag", 0, 0, NULL);
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    event = sceKernelCreateSimpleEvent("event", 0, 0, NULL);
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 64, NULL);
    sceKernelCreateLwMutex(&lw_mutex, "lw_mutex", 0, 0, NULL);
    sceKernelCreateLwMutex(&lw_cond_mutex, "lw_cond_mutex", 0, 0, NULL);
    sceKernelCreateLwCond(&lw_cond, "lw_cond", 0, &lw_cond_mutex, NULL);

    // A thread that isn't waiting has no wait type
    SceKernelThreadInfo info = { .size = sizeof(info) };
    PRINT("info of the running thread", sceKernelGetThreadInfo(sceKernelGetThreadId(), &info));
    PRINT("the running thread has no wait type", info.waitType);

    print_wait("delay", DELAY, 0);
    print_wait("delay with callbacks", DELAY_CB, 0);
    print_wait("semaphore", SEMA, sema);
    print_wait("semaphore with callbacks", SEMA_CB, sema);
    print_wait("event flag", EVENT_FLAG, flag);
    print_wait("simple event", SIMPLE_EVENT, event);
    print_wait("message pipe", MSG_PIPE, pipe);

    sceKernelLockMutex(mutex, 1, NULL);
    print_wait("mutex", MUTEX, mutex);
    sceKernelUnlockMutex(mutex, 1);
    print_wait("condition variable", COND, cond);

    sceKernelLockWriteRWLock(rwlock, NULL);
    print_wait("reader/writer lock", RW_LOCK, rwlock);
    sceKernelUnlockWriteRWLock(rwlock);

    sceKernelLockLwMutex(&lw_mutex, 1, NULL);
    print_wait("lightweight mutex", LW_MUTEX, 0);
    sceKernelUnlockLwMutex(&lw_mutex, 1);
    print_wait("lightweight condition variable", LW_COND, 0);

    sleeper = test_start_thread(sleep_thread);
    print_wait("thread end", THREAD_END, sleeper);
    test_join_thread(sleeper);

    test_exit();
    return 0;
}
