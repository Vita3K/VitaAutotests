// Where a waiting thread goes in a priority ordered queue when its priority is
// raised while it waits, for the kinds priority_requeue_others leaves out:
// semaphore, read lock waiters, timer, lightweight condition, thread end
// waiters and message pipe senders. The model covers the mutex, event flag,
// simple event, message pipe receivers, write lock, lightweight mutex and
// condition kinds. The setup is the model's: two low priority waiters, then one
// at a higher priority, then the second low one raised to that priority. All
// the waiters are pinned to core 0, so the waiters that one release wakes
// together (read lock waiters and thread end waiters) run in a fixed order.
// Each other kind lets one waiter through per release: a signal, a timer event
// set and started, a lightweight condition signal, or a one byte receive from a
// full pipe, where the senders have attribute 0x1 and queue by priority. The
// order is recorded by each waiter when it gets its wait.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define LOW_PRIORITY (SCE_KERNEL_DEFAULT_PRIORITY_USER + 2)
#define MID_PRIORITY SCE_KERNEL_DEFAULT_PRIORITY_USER

// The message pipe attribute that queues its waiting senders by priority, as
// msgpipe_sender_requeue shows. The header has no name for it.
#define MSG_PIPE_ATTR_SEND_PRIO 0x1

// The delay of each timer event, long enough that the waiter of the event is
// waiting by then
#define TIMER_INTERVAL_US 100000
#define PIPE_SIZE 4096

enum Kind { SEMA, READ_LOCK, TIMER, LW_COND, THREAD_END, PIPE_SEND, KIND_COUNT };
static const char *const kind_names[] = {
    "semaphore",          "read lock waiters",    "timer", "lightweight condition",
    "thread end waiters", "message pipe senders",
};

static SceUID object, gate;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static char fill[PIPE_SIZE];
static volatile int kind, order[3], woken;

// Records that waiter i got its wait, and the place it got in the order
static void record(int i) { order[i] = __atomic_add_fetch(&woken, 1, __ATOMIC_SEQ_CST); }

// The thread that the thread end waiters wait for, which ends once the test
// signals the gate
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
    SceUInt timeout = TEST_WAIT_US;
    SceUInt32 bits;
    char byte = 'x';
    switch (kind) {
    case SEMA:
        if (sceKernelWaitSema(object, 1, &timeout) == 0)
            record(i);
        break;
    case READ_LOCK:
        if (sceKernelLockReadRWLock(object, &timeout) == 0) {
            record(i);
            sceKernelUnlockReadRWLock(object);
        }
        break;
    case TIMER:
        if (sceKernelWaitEvent(object, EVENT_TIMER, &bits, NULL, &timeout) == 0)
            record(i);
        break;
    case LW_COND:
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        if (sceKernelWaitLwCond(&lw_cond, &timeout) == 0)
            record(i);
        sceKernelUnlockLwMutex(&lw_mutex, 1);
        break;
    case THREAD_END:
        if (sceKernelWaitThreadEnd(object, NULL, &timeout) == 0)
            record(i);
        break;
    case PIPE_SEND:
        if (sceKernelSendMsgPipe(object, &byte, 1, MSG_PIPE_MODE_FULL, NULL, &timeout) == 0)
            record(i);
        break;
    }
    return 0;
}

static SceUID start(int i, int priority) {
    const SceUID thread = sceKernelCreateThread("waiter", waiter, priority, 0x4000, 0,
                                                SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, sizeof(i), &i);
    test_wait_until_waiting(thread);
    return thread;
}

// Lets one more waiter through, for the kinds where a release lets one waiter
// through
static void release_one(void) {
    char byte = 'x';
    SceUInt timeout = TEST_WAIT_US;
    SceUInt64 interval = TIMER_INTERVAL_US;
    switch (kind) {
    case SEMA:
        sceKernelSignalSema(object, 1);
        break;
    case TIMER:
        sceKernelSetTimerEvent(object, TIMER_TYPE_SET_EVENT, &interval, 0);
        sceKernelStartTimer(object);
        break;
    case LW_COND:
        sceKernelSignalLwCond(&lw_cond);
        break;
    case PIPE_SEND:
        sceKernelReceiveMsgPipe(object, &byte, 1, MSG_PIPE_MODE_FULL, NULL, &timeout);
        break;
    }
}

static void run(int new_kind) {
    kind = new_kind;
    woken = order[0] = order[1] = order[2] = 0;
    const char *name = kind_names[new_kind];
    int created = 0;
    switch (new_kind) {
    case SEMA:
        created = object = sceKernelCreateSema("sema", SCE_KERNEL_ATTR_THREAD_PRIO, 0, 3, NULL);
        break;
    case READ_LOCK:
        created = object = sceKernelCreateRWLock("rwlock", SCE_KERNEL_ATTR_THREAD_PRIO, NULL);
        sceKernelLockWriteRWLock(object, NULL);
        break;
    case TIMER:
        created = object = sceKernelCreateTimer(
            "timer", EVENT_ATTR_AUTO_RESET | SCE_KERNEL_ATTR_THREAD_PRIO, NULL);
        break;
    case LW_COND:
        sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 0, NULL);
        created = sceKernelCreateLwCond(&lw_cond, "lw cond", SCE_KERNEL_ATTR_THREAD_PRIO, &lw_mutex,
                                        NULL);
        break;
    // The thread has no priority attribute, it is the object the waiters wait for
    // the end of
    case THREAD_END:
        gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
        created = object = test_start_thread(target_entry);
        break;
    case PIPE_SEND:
        created = object = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER,
                                                  MSG_PIPE_ATTR_SEND_PRIO, PIPE_SIZE, NULL);
        sceKernelTrySendMsgPipe(object, fill, PIPE_SIZE, MSG_PIPE_MODE_FULL, NULL);
        break;
    }
    PRINTF(test_created(created), "%s: create the object", name);
    if (created < 0)
        return;

    SceUID threads[3];
    threads[0] = start(0, LOW_PRIORITY);
    threads[1] = start(1, LOW_PRIORITY);
    threads[2] = start(2, MID_PRIORITY);
    sceKernelChangeThreadPriority(threads[1], MID_PRIORITY);

    switch (new_kind) {
    case READ_LOCK:
        sceKernelUnlockWriteRWLock(object);
        TEST_WAIT_FOR(woken == 3);
        break;
    case THREAD_END:
        sceKernelSignalSema(gate, 1);
        TEST_WAIT_FOR(woken == 3);
        break;
    default:
        for (int i = 0; i < 3; i++) {
            release_one();
            TEST_WAIT_FOR(woken == i + 1);
        }
        break;
    }
    for (int i = 0; i < 3; i++)
        test_join_thread(threads[i]);
    PRINTF(order[0], "%s: the low priority waiter that waited first", name);
    PRINTF(order[1], "%s: the raised waiter", name);
    PRINTF(order[2], "%s: the waiter already at that priority", name);

    switch (new_kind) {
    case SEMA:
        sceKernelDeleteSema(object);
        break;
    case READ_LOCK:
        sceKernelDeleteRWLock(object);
        break;
    case TIMER:
        sceKernelStopTimer(object);
        sceKernelDeleteTimer(object);
        break;
    case LW_COND:
        sceKernelDeleteLwCond(&lw_cond);
        sceKernelDeleteLwMutex(&lw_mutex);
        break;
    case THREAD_END:
        test_join_thread(object);
        sceKernelDeleteSema(gate);
        break;
    case PIPE_SEND:
        sceKernelDeleteMsgPipe(object);
        break;
    }
}

int main(void) {
    for (int k = 0; k < KIND_COUNT; k++)
        run(k);

    test_exit();
    return 0;
}
