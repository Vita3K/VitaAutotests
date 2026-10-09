// Who wakes first when several threads wait
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define LOW_PRIORITY 0x10000102
#define HIGH_PRIORITY 0x100000FE

static SceUID sema, mutex, rwlock, pipe;
static int need[3], order[3], woken;

static void reset_order(void) {
    woken = 0;
    for (int i = 0; i < 3; i++)
        order[i] = 0;
}

static SceUID start(SceKernelThreadEntry entry, int i, int priority) {
    const SceUID thread = sceKernelCreateThread("waiter", entry, priority, 0x4000, 0, 0, NULL);
    sceKernelStartThread(thread, sizeof(i), &i);
    test_wait_until_waiting(thread);
    return thread;
}

static void join_all(const SceUID *threads, int count) {
    for (int i = 0; i < count; i++)
        test_join_thread(threads[i]);
}

static int sema_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelWaitSema(sema, need[i], &timeout) == 0)
        order[i] = ++woken;
    return 0;
}

static int mutex_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelLockMutex(mutex, 1, &timeout) == 0) {
        order[i] = ++woken;
        sceKernelUnlockMutex(mutex, 1);
    }
    return 0;
}

// Slots 0 and 2 read lock, slot 1 write locks. Each keeps the lock a while so the order shows.
static int rwlock_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    unsigned int timeout = TEST_WAIT_US;
    const int write = i == 1;
    if ((write ? sceKernelLockWriteRWLock(rwlock, &timeout) : sceKernelLockReadRWLock(rwlock, &timeout)) == 0) {
        order[i] = ++woken;
        sceKernelDelayThread(20000);
        write ? sceKernelUnlockWriteRWLock(rwlock) : sceKernelUnlockReadRWLock(rwlock);
    }
    return 0;
}

static char received[3];

static int pipe_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelReceiveMsgPipe(pipe, &received[i], 1, MSG_PIPE_MODE_FULL, NULL, &timeout) == 0)
        order[i] = ++woken;
    return 0;
}

int main(void) {
    SceUID threads[3];

    // A FIFO semaphore with a big request first: does a later small one get past it?
    sema = sceKernelCreateSema("fifo", 0, 0, 5, NULL);
    reset_order();
    need[0] = 3;
    need[1] = 1;
    threads[0] = start(sema_thread, 0, 0x10000100);
    threads[1] = start(sema_thread, 1, 0x10000100);
    sceKernelSignalSema(sema, 1);
    test_let_threads_wait();
    PRINT("a small request behind a big one, after a signal of 1", order[1]);
    PRINT("the big request", order[0]);
    sceKernelSignalSema(sema, 3);
    join_all(threads, 2);
    PRINT("the big request after 3 more", order[0] != 0);
    PRINT("the small request after 3 more", order[1] != 0);
    sceKernelDeleteSema(sema);

    // A priority semaphore where the waiting thread's priority changes while it waits
    sema = sceKernelCreateSema("prio", ATTR_TH_PRIO, 0, 5, NULL);
    reset_order();
    need[0] = need[1] = 1;
    threads[0] = start(sema_thread, 0, LOW_PRIORITY);
    threads[1] = start(sema_thread, 1, HIGH_PRIORITY);
    sceKernelChangeThreadPriority(threads[0], 0x100000F0);
    sceKernelSignalSema(sema, 1);
    test_let_threads_wait();
    PRINT("the waiter raised above the other while waiting wakes first", order[0]);
    sceKernelSignalSema(sema, 1);
    join_all(threads, 2);
    sceKernelDeleteSema(sema);

    // Unlocking a FIFO mutex with a waiter, then trying to take it back right away
    mutex = sceKernelCreateMutex("mutex", 0, 1, NULL);
    reset_order();
    threads[0] = start(mutex_thread, 0, 0x10000100);
    sceKernelUnlockMutex(mutex, 1);
    PRINT("try lock right after unlocking to a waiter", sceKernelTryLockMutex(mutex, 1));
    sceKernelUnlockMutex(mutex, 1);
    join_all(threads, 1);
    PRINT("the waiter got it", order[0] != 0);

    // A reader, a writer and a reader waiting behind a writer
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    sceKernelLockWriteRWLock(rwlock, NULL);
    reset_order();
    for (int i = 0; i < 3; i++)
        threads[i] = start(rwlock_thread, i, 0x10000100);
    sceKernelUnlockWriteRWLock(rwlock);
    join_all(threads, 3);
    PRINT("the first waiting reader goes before the writer", order[0] < order[1]);
    PRINT("the reader behind the writer goes before it", order[2] < order[1]);
    PRINT("the waiting writer's turn", order[1]);

    // Receivers of a FIFO message pipe, then of a priority one
    pipe = sceKernelCreateMsgPipe("fifo", MSG_PIPE_TYPE_USER, 0, 16, NULL);
    reset_order();
    threads[0] = start(pipe_thread, 0, LOW_PRIORITY);
    threads[1] = start(pipe_thread, 1, HIGH_PRIORITY);
    sceKernelSendMsgPipe(pipe, "a", 1, MSG_PIPE_MODE_FULL, NULL, NULL);
    test_let_threads_wait();
    sceKernelSendMsgPipe(pipe, "b", 1, MSG_PIPE_MODE_FULL, NULL, NULL);
    join_all(threads, 2);
    PRINT("the first FIFO receiver gets the first byte", received[0]);
    PRINT("the second FIFO receiver gets the second byte", received[1]);
    // A message pipe has its own queue order bits, so this tries each one that a create accepts
    static const unsigned int attrs[] = { 0x1, 0x2, 0x4, 0x8 };
    for (unsigned int a = 0; a < sizeof(attrs) / sizeof(attrs[0]); a++) {
        char label[80];
        pipe = sceKernelCreateMsgPipe("prio", MSG_PIPE_TYPE_USER, attrs[a], 16, NULL);
        sceClibSnprintf(label, sizeof(label), "create a pipe with attribute 0x%X", attrs[a]);
        PRINT(label, test_created(pipe));
        if (pipe < 0)
            continue;
        received[0] = received[1] = 0;
        threads[0] = start(pipe_thread, 0, LOW_PRIORITY);
        threads[1] = start(pipe_thread, 1, HIGH_PRIORITY);
        sceKernelSendMsgPipe(pipe, "a", 1, MSG_PIPE_MODE_FULL, NULL, NULL);
        test_let_threads_wait();
        sceKernelSendMsgPipe(pipe, "b", 1, MSG_PIPE_MODE_FULL, NULL, NULL);
        join_all(threads, 2);
        sceClibSnprintf(label, sizeof(label), "the low priority receiver, which waited first, gets with 0x%X", attrs[a]);
        PRINT(label, received[0]);
        sceKernelDeleteMsgPipe(pipe);
    }

    test_exit();
    return 0;
}
