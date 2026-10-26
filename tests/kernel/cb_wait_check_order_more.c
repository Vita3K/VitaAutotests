// Whether a wait with callbacks rejects a bad argument, or a condition wait without its mutex,
// before it runs the callbacks that are pending. The model cb_wait_check_order covers the event
// flag cases and the semaphore count cases. This test repeats the semaphore cases and adds the
// other kinds of wait. Every case runs on its own thread T, pinned to core 0, as the main thread
// does too. T creates a callback, notifies it once so that it is pending, makes the wait with
// callbacks and records the wait's result and how often the callback had run when the wait
// returned. Then T calls sceKernelCheckCallback and records its result and how often the callback
// had run after it. The main thread waits for T to end and prints. Every wait has a finite timeout.
// The cases:
// - sceKernelWaitSemaCB with a count of 0, and with a count above the semaphore's maximum
// - sceKernelLockMutexCB with a count of 0, and with a count of 2 on a free non recursive mutex
// - sceKernelLockLwMutexCB with a count of 0, and with a count of 2 on a free non recursive
//   lightweight mutex
// - sceKernelWaitEventCB with the pattern 0x1, which is not a user bit
// - sceKernelReceiveMsgPipeCB and sceKernelSendMsgPipeCB with the mode 0x8, and with a size of 8
//   KiB on a pipe whose buffer is 4 KiB
// - sceKernelWaitCondCB on a condition whose mutex T doesn't hold
// - sceKernelWaitSignalCB with 2 as its first argument
// - sceKernelWaitThreadEndCB on UID 0, and on T's own UID. The own UID case is the last one, since
//   a wrong implementation may hang on it, as the wait_own_end test explains
// Each case prints the wait's result, the runs of the callback when the wait returned, the check's
// result and the runs after the check.
//
// On a PS Vita TV on firmware 3.65 the semaphore, mutex, lightweight mutex, message pipe, condition
// and thread end arguments end the wait with an error before the callback runs, so the callback has
// not run when the wait returns, and the check that follows runs it and returns 1. The simple event
// pattern 0x1 ends it with ILLEGAL_PATTERN the same way. The thread signal wait takes 1 as its
// first argument without complaint, so the first argument 2 is most likely ignored too: the wait
// then sleeps its whole timeout, with the callback run once before the sleep, and ends with
// WAIT_TIMEOUT.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define PIPE_BUFFER 4096
#define PIPE_TOO_BIG 8192
#define BAD_PIPE_MODE 0x8

typedef enum Kind {
    SEMA_ZERO,
    SEMA_ABOVE_MAX,
    MUTEX_ZERO,
    MUTEX_TWO,
    LW_ZERO,
    LW_TWO,
    EVENT_PATTERN_ONE,
    PIPE_RECEIVE_MODE,
    PIPE_RECEIVE_SIZE,
    PIPE_SEND_MODE,
    PIPE_SEND_SIZE,
    COND_NOT_HELD,
    SIGNAL_ARG_TWO,
    END_UID_ZERO,
    END_OWN_UID,
} Kind;

static Kind kind;
static SceUID object, callback, mutex;
static SceKernelLwMutexWork lw_mutex;
static SceUInt32 bits;
static SceUInt64 user_data;
static char small[4];
static char big[PIPE_TOO_BIG];
static volatile int callback_runs, done;
static volatile int wait_result, runs_in_wait, check_result, runs_after_check;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    return 0;
}

// Makes the wait of the case, with callbacks
static int make_wait(void) {
    SceUInt timeout = TEST_WAIT_US;
    switch (kind) {
    case SEMA_ZERO: return sceKernelWaitSemaCB(object, 0, &timeout);
    case SEMA_ABOVE_MAX: return sceKernelWaitSemaCB(object, 3, &timeout);
    case MUTEX_ZERO: return sceKernelLockMutexCB(object, 0, &timeout);
    case MUTEX_TWO: return sceKernelLockMutexCB(object, 2, &timeout);
    case LW_ZERO: return sceKernelLockLwMutexCB(&lw_mutex, 0, &timeout);
    case LW_TWO: return sceKernelLockLwMutexCB(&lw_mutex, 2, &timeout);
    case EVENT_PATTERN_ONE: return sceKernelWaitEventCB(object, 0x1, &bits, &user_data, &timeout);
    case PIPE_RECEIVE_MODE: return sceKernelReceiveMsgPipeCB(object, small, sizeof(small), BAD_PIPE_MODE, NULL, &timeout);
    case PIPE_RECEIVE_SIZE: return sceKernelReceiveMsgPipeCB(object, big, sizeof(big), MSG_PIPE_MODE_FULL, NULL, &timeout);
    case PIPE_SEND_MODE: return sceKernelSendMsgPipeCB(object, small, sizeof(small), BAD_PIPE_MODE, NULL, &timeout);
    case PIPE_SEND_SIZE: return sceKernelSendMsgPipeCB(object, big, sizeof(big), MSG_PIPE_MODE_FULL, NULL, &timeout);
    case COND_NOT_HELD: return sceKernelWaitCondCB(object, &timeout);
    case SIGNAL_ARG_TWO: return sceKernelWaitSignalCB(2, 0, &timeout);
    case END_UID_ZERO: return sceKernelWaitThreadEndCB(0, NULL, &timeout);
    case END_OWN_UID: return sceKernelWaitThreadEndCB(sceKernelGetThreadId(), NULL, &timeout);
    }
    return NOT_DONE;
}

// Notifies its own callback once, then makes the wait of the case and checks the callbacks
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("pending", 0, callback_entry, NULL);
    sceKernelNotifyCallback(callback, 1);
    wait_result = make_wait();
    runs_in_wait = callback_runs;
    check_result = sceKernelCheckCallback();
    runs_after_check = callback_runs;
    sceKernelDeleteCallback(callback);
    done = 1;
    return 0;
}

static SceUID start(SceKernelThreadEntry entry) {
    const SceUID thread = sceKernelCreateThread("t", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

// Creates the object the wait of the case uses
static void setup(Kind k) {
    switch (k) {
    case SEMA_ZERO:
    case SEMA_ABOVE_MAX: object = sceKernelCreateSema("sema", 0, 1, 2, NULL); break;
    case MUTEX_ZERO:
    case MUTEX_TWO: object = sceKernelCreateMutex("mutex", 0, 0, NULL); break;
    case LW_ZERO:
    case LW_TWO: sceKernelCreateLwMutex(&lw_mutex, "lw", 0, 0, NULL); break;
    case EVENT_PATTERN_ONE: object = sceKernelCreateSimpleEvent("event", 0, 0, NULL); break;
    case PIPE_RECEIVE_MODE:
    case PIPE_RECEIVE_SIZE:
    case PIPE_SEND_MODE:
    case PIPE_SEND_SIZE: object = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, PIPE_BUFFER, NULL); break;
    case COND_NOT_HELD:
        mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
        object = sceKernelCreateCond("cond", 0, mutex, NULL);
        break;
    case SIGNAL_ARG_TWO:
    case END_UID_ZERO:
    case END_OWN_UID: break;
    }
}

// Deletes what setup created
static void cleanup(Kind k) {
    switch (k) {
    case SEMA_ZERO:
    case SEMA_ABOVE_MAX: sceKernelDeleteSema(object); break;
    case MUTEX_ZERO:
    case MUTEX_TWO: sceKernelDeleteMutex(object); break;
    case LW_ZERO:
    case LW_TWO: sceKernelDeleteLwMutex(&lw_mutex); break;
    case EVENT_PATTERN_ONE: sceKernelDeleteSimpleEvent(object); break;
    case PIPE_RECEIVE_MODE:
    case PIPE_RECEIVE_SIZE:
    case PIPE_SEND_MODE:
    case PIPE_SEND_SIZE: sceKernelDeleteMsgPipe(object); break;
    case COND_NOT_HELD:
        sceKernelDeleteCond(object);
        sceKernelDeleteMutex(mutex);
        break;
    case SIGNAL_ARG_TWO:
    case END_UID_ZERO:
    case END_OWN_UID: break;
    }
}

static void run_case(const char *name, Kind k) {
    kind = k;
    wait_result = runs_in_wait = check_result = runs_after_check = NOT_DONE;
    callback_runs = done = 0;
    setup(k);
    const SceUID t = start(t_entry);
    TEST_WAIT_FOR(done);
    test_join_thread(t);
    cleanup(k);

    PRINTF(wait_result, "%s: the wait", name);
    PRINTF(runs_in_wait, "%s: runs of the callback when the wait returned", name);
    PRINTF(check_result, "%s: the check's result", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("semaphore, count 0", SEMA_ZERO);
    run_case("semaphore, count above the maximum", SEMA_ABOVE_MAX);
    run_case("mutex, lock count 0", MUTEX_ZERO);
    run_case("mutex, lock count 2", MUTEX_TWO);
    run_case("lightweight mutex, lock count 0", LW_ZERO);
    run_case("lightweight mutex, lock count 2", LW_TWO);
    run_case("simple event, pattern 0x1", EVENT_PATTERN_ONE);
    run_case("message pipe receive, mode 0x8", PIPE_RECEIVE_MODE);
    run_case("message pipe receive, size above the buffer", PIPE_RECEIVE_SIZE);
    run_case("message pipe send, mode 0x8", PIPE_SEND_MODE);
    run_case("message pipe send, size above the buffer", PIPE_SEND_SIZE);
    run_case("condition, mutex not held", COND_NOT_HELD);
    run_case("thread signal, first argument 2", SIGNAL_ARG_TWO);
    run_case("thread end, UID 0", END_UID_ZERO);
    run_case("thread end, own UID", END_OWN_UID);

    test_exit();
    return 0;
}
