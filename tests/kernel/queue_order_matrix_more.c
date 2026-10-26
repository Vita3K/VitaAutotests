// The order three more kinds of wait queue wake three waiters in, for the same attributes and
// priorities queue_order_matrix uses: rwlock readers behind a writer, message pipe senders and
// thread end waiters. The model covers the semaphore, event flag, mutex, lightweight mutex, rwlock
// writers, condition, lightweight condition, auto reset simple event and message pipe receivers.
// The auto reset timer is in queue_order_timer. Readers take attributes 0 (FIFO) and 0x2000,
// message pipe senders take each of the attribute bits 0x0, 0x1, 0x2, 0x4 and 0x8, and thread end
// waiters have no attribute. The waiters queue in the order 0, 1, 2, each once the one before
// waits, either at the priorities 0x90, 0x70, 0x80 or all at 0x80, and once more at 0x90, 0x70,
// 0x80 with waiter 2 raised to 0x60 while all three wait. Each pipe receive or signal lets one
// waiter through. The readers, and the thread end waiters, all wake at one release, so they are
// pinned to core 0 and the order they got their wait in is the order they ran in. The order prints
// as one hex digit per waiter, first woken first, so 0x012 is FIFO order and 0x120 is priority
// order for the first set of priorities.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// The message pipe attribute that queues its waiting senders by priority, as msgpipe_sender_requeue
// shows. The header has no name for it.
#define MSG_PIPE_ATTR_SEND_PRIO 0x1

#define PIPE_SIZE 4096

typedef enum Kind { RW_READ, PIPE_SEND, THREAD_END } Kind;

static Kind kind;
static SceUID object, gate;
static char fill[PIPE_SIZE];
static volatile int done_count;
static int order[3];
static volatile int result[3];

static void woke(int i) {
    const int n = __atomic_fetch_add(&done_count, 1, __ATOMIC_SEQ_CST);
    if (n < 3)
        order[n] = i;
}

// The thread that the thread end waiters wait for, which ends once the test signals the gate
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    char byte = 'x';
    switch (kind) {
    case RW_READ:
        if ((result[i] = sceKernelLockReadRWLock(object, &timeout)) == 0) {
            woke(i);
            sceKernelUnlockReadRWLock(object);
        }
        break;
    case PIPE_SEND:
        if ((result[i] =
                 sceKernelSendMsgPipe(object, &byte, 1, MSG_PIPE_MODE_FULL, NULL, &timeout)) == 0)
            woke(i);
        break;
    case THREAD_END:
        if ((result[i] = sceKernelWaitThreadEnd(object, NULL, &timeout)) == 0)
            woke(i);
        break;
    }
    return 0;
}

static void create(Kind k, SceUInt32 attr) {
    switch (k) {
    case RW_READ:
        object = sceKernelCreateRWLock("rw", attr, NULL);
        sceKernelLockWriteRWLock(object, NULL);
        break;
    case PIPE_SEND:
        object = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, attr, PIPE_SIZE, NULL);
        sceKernelTrySendMsgPipe(object, fill, PIPE_SIZE, MSG_PIPE_MODE_FULL, NULL);
        break;
    case THREAD_END:
        gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
        object = test_start_thread(target_entry);
        break;
    }
}

static void destroy(Kind k) {
    switch (k) {
    case RW_READ:
        sceKernelDeleteRWLock(object);
        break;
    case PIPE_SEND:
        sceKernelDeleteMsgPipe(object);
        break;
    case THREAD_END:
        test_join_thread(object);
        sceKernelDeleteSema(gate);
        break;
    }
}

// Lets one more waiter through, or for the readers and the thread end waiters all of them at once
static void release(Kind k) {
    char byte = 'x';
    SceUInt timeout = TEST_WAIT_US;
    switch (k) {
    case RW_READ:
        sceKernelUnlockWriteRWLock(object);
        break;
    case PIPE_SEND:
        sceKernelReceiveMsgPipe(object, &byte, 1, MSG_PIPE_MODE_FULL, NULL, &timeout);
        break;
    case THREAD_END:
        sceKernelSignalSema(gate, 1);
        break;
    }
}

// Whether one release lets all the waiters through, so the test releases once and waits for all
static int one_release(Kind k) { return k == RW_READ || k == THREAD_END; }

static void run(Kind k, const char *name, SceUInt32 attr, const int *priorities,
                const char *priority_name, int raise_last) {
    kind = k;
    done_count = 0;
    for (int i = 0; i < 3; i++) {
        order[i] = 0xF;
        result[i] = 0xDEAD;
    }
    create(k, attr);
    SceUID threads[3];
    for (int i = 0; i < 3; i++) {
        threads[i] = sceKernelCreateThread("waiter", waiter, priorities[i], 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(threads[i], sizeof(i), &i);
        test_wait_until_waiting(threads[i]);
    }
    if (raise_last)
        sceKernelChangeThreadPriority(threads[2], 0x60);
    if (one_release(k)) {
        release(k);
        TEST_WAIT_FOR(done_count == 3);
    } else {
        for (int n = 0; n < 3; n++) {
            release(k);
            TEST_WAIT_FOR(done_count > n);
        }
    }
    for (int i = 0; i < 3; i++)
        test_join_thread(threads[i]);
    destroy(k);
    const int value = (order[0] << 8) | (order[1] << 4) | order[2];
    if (k == THREAD_END)
        PRINTF(value, "%s, priorities %s: wake order", name, priority_name);
    else
        PRINTF(value, "%s, attr 0x%X, priorities %s: wake order", name, (unsigned)attr,
               priority_name);
    if (done_count != 3)
        PRINTF(done_count, "%s, attr 0x%X, priorities %s: waiters woken", name, (unsigned)attr,
               priority_name);
}

int main(void) {
    static const int mixed[3] = { 0x90, 0x70, 0x80 };
    static const int same[3] = { 0x80, 0x80, 0x80 };
    static const SceUInt32 attrs[] = { 0, SCE_KERNEL_ATTR_THREAD_PRIO };
    static const SceUInt32 pipe_attrs[] = { 0, 0x1, 0x2, 0x4, 0x8 };

    for (unsigned a = 0; a < sizeof(attrs) / sizeof(attrs[0]); a++) {
        run(RW_READ, "rwlock readers behind a writer", attrs[a], mixed, "0x90 0x70 0x80", 0);
        run(RW_READ, "rwlock readers behind a writer", attrs[a], same, "all 0x80", 0);
        run(RW_READ, "rwlock readers behind a writer", attrs[a], mixed,
            "0x90 0x70 0x80, then waiter 2 raised to 0x60", 1);
    }
    for (unsigned a = 0; a < sizeof(pipe_attrs) / sizeof(pipe_attrs[0]); a++) {
        run(PIPE_SEND, "message pipe senders", pipe_attrs[a], mixed, "0x90 0x70 0x80", 0);
        run(PIPE_SEND, "message pipe senders", pipe_attrs[a], same, "all 0x80", 0);
        run(PIPE_SEND, "message pipe senders", pipe_attrs[a], mixed,
            "0x90 0x70 0x80, then waiter 2 raised to 0x60", 1);
    }
    run(THREAD_END, "thread end waiters", 0, mixed, "0x90 0x70 0x80", 0);
    run(THREAD_END, "thread end waiters", 0, same, "all 0x80", 0);
    run(THREAD_END, "thread end waiters", 0, mixed, "0x90 0x70 0x80, then waiter 2 raised to 0x60",
        1);

    test_exit();
    return 0;
}
