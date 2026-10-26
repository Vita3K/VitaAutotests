// What a wait with callbacks on a condition variable does when the condition or its mutex is
// deleted while the waiter runs a callback, out of the condition's queue, and what happens when the
// callback deletes the condition itself. A thread T locks a mutex M and waits on a condition C with
// callbacks, which releases M. The main thread notifies T's callback. Unless a case says otherwise,
// the callback waits on a gate semaphore with a timeout of 3 s, so T is out of C's queue until the
// main thread opens the gate. T's wait has a timeout of 4 s, or 2 s where a case says so. The main
// thread runs on core 0 at 0x60 and T is pinned to core 0 at 0xB0. Every case uses fresh objects.
// The cases:
// - delete C while the callback waits on the gate, then open the gate. Heavy and lightweight.
// - heavy only: delete M while the callback waits on the gate, then open the gate. The main thread
//   checks whether T's wait returned within 1 s of the gate, then signals C, which ends T's wait if
//   the delete didn't.
// - heavy only, the second run: delete M while the callback waits on the gate, then open the gate,
//   with a timeout of 2 s and no signal. Checks that T's wait lasts at least 1.75 s.
// - the callback deletes C itself, without a gate. Checks whether T's wait returns within 1 s of
//   the notify. Heavy and lightweight.
// - heavy only, in the relock: the main thread holds M and signals C, so T waits to lock M again
//   with callbacks. The main thread notifies T's callback, which waits on the gate, deletes M, then
//   opens the gate. Checks whether T's wait returns within 1 s of the gate.
// Each case prints the results of its calls, the callback's runs, and T's unlock of M afterwards.
// The lightweight mutex is never deleted, so each lightweight case gets a mutex of its own.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define WAIT_US 4000000
#define SHORT_WAIT_US 2000000
#define GATE_US 3000000
#define SOON_US 1000000
#define LATE_US 1750000

// What the callback does: blocks on the gate, or deletes the condition
#define CB_GATE 0
#define CB_DELETE_COND 1

// One kind of condition variable, with the calls that differ
typedef struct Kind {
    const char *name;
    SceUInt32 cb_wait_type;
    void (*create)(void);
    void (*destroy)(void);
    int (*lock)(void);
    int (*unlock)(void);
    int (*wait_cb)(SceUInt *timeout);
    int (*signal)(void);
    int (*delete_cond)(void);
} Kind;

static const Kind *kind;
static SceUID mutex, cond, gate, callback;
static SceKernelLwMutexWork lw_mutexes[4];
static int lw_mutexes_used;
static SceKernelLwMutexWork *lw_mutex;
static SceKernelLwCondWork lw_cond;
// The UID a wait on the condition reports, which the create calls store
static SceUID cond_id;
static volatile int action, callback_ready, callback_runs, delete_in_callback;
static volatile int t_result, t_unlock, t_done;
static volatile SceUInt wait_us;
static volatile SceUInt64 t_start, t_end;

static void heavy_create(void) {
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    cond_id = cond;
}

static void heavy_destroy(void) {
    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(mutex);
}

static int heavy_lock(void) {
    return sceKernelLockMutex(mutex, 1, NULL);
}

static int heavy_unlock(void) {
    return sceKernelUnlockMutex(mutex, 1);
}

static int heavy_wait_cb(SceUInt *timeout) {
    return sceKernelWaitCondCB(cond, timeout);
}

static int heavy_signal(void) {
    return sceKernelSignalCond(cond);
}

static int heavy_delete_cond(void) {
    return sceKernelDeleteCond(cond);
}

static void lw_create(void) {
    lw_mutex = &lw_mutexes[lw_mutexes_used++];
    sceKernelCreateLwMutex(lw_mutex, "lw mutex", 0, 0, NULL);
    sceKernelCreateLwCond(&lw_cond, "lw cond", 0, lw_mutex, NULL);
    LwCondInfo info = { .size = sizeof(info) };
    sceKernelGetLwCondInfo(&lw_cond, &info);
    cond_id = info.uid;
}

static void lw_destroy(void) {
    sceKernelDeleteLwCond(&lw_cond);
}

static int lw_lock(void) {
    return sceKernelLockLwMutex(lw_mutex, 1, NULL);
}

static int lw_unlock(void) {
    return sceKernelUnlockLwMutex(lw_mutex, 1);
}

static int lw_wait_cb(SceUInt *timeout) {
    return sceKernelWaitLwCondCB(&lw_cond, timeout);
}

static int lw_signal(void) {
    return sceKernelSignalLwCond(&lw_cond);
}

static int lw_delete_cond(void) {
    return sceKernelDeleteLwCond(&lw_cond);
}

static const Kind kinds[2] = {
    { "heavy", WAIT_TYPE_COND | WAIT_TYPE_CB, heavy_create, heavy_destroy, heavy_lock, heavy_unlock,
        heavy_wait_cb, heavy_signal, heavy_delete_cond },
    { "lightweight", WAIT_TYPE_LW_COND | WAIT_TYPE_CB, lw_create, lw_destroy, lw_lock, lw_unlock,
        lw_wait_cb, lw_signal, lw_delete_cond },
};

// Runs in T, out of the condition's queue. Blocks on the gate, or deletes the condition.
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    if (action == CB_GATE) {
        SceUInt timeout = GATE_US;
        sceKernelWaitSema(gate, 1, &timeout);
    } else {
        delete_in_callback = kind->delete_cond();
    }
    return 0;
}

// Locks M, waits on C with callbacks, and records how the wait ended and the time it took
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    kind->lock();
    callback_ready = 1;
    SceUInt timeout = wait_us;
    t_start = sceKernelGetProcessTimeWide();
    t_result = kind->wait_cb(&timeout);
    t_end = sceKernelGetProcessTimeWide();
    t_unlock = kind->unlock();
    sceKernelDeleteCallback(callback);
    t_done = 1;
    return 0;
}

// Sets the state of a case and creates its objects and gate
static void setup(const Kind *k, int cb_action, SceUInt timeout) {
    kind = k;
    action = cb_action;
    wait_us = timeout;
    callback_ready = callback_runs = delete_in_callback = t_done = 0;
    t_result = t_unlock = NOT_DONE;
    t_start = t_end = 0;
    k->create();
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
}

// Deletes what the case left of its objects
static void teardown(void) {
    kind->destroy();
    sceKernelDeleteSema(gate);
}

static SceUID start_t(void) {
    const SceUID t = sceKernelCreateThread(
        "t", t_entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    return t;
}

// Waits up to timeout for thread t to end, and deletes it if it did. Returns whether it ended.
static int wait_end(SceUID t, SceUInt timeout) {
    const int ended = sceKernelWaitThreadEnd(t, NULL, &timeout) == 0;
    if (ended)
        sceKernelDeleteThread(t);
    return ended;
}

// Waits until T has recorded its results, then joins it
static void join_t(SceUID t) {
    TEST_WAIT_FOR(t_done);
    wait_end(t, TEST_WAIT_US);
}

// The callback waits on the gate, the main thread deletes C, then opens the gate
static void delete_cond_case(const Kind *k) {
    const char *const name = "delete the condition";
    setup(k, CB_GATE, WAIT_US);
    const SceUID t = start_t();
    TEST_WAIT_FOR(callback_ready && test_in_wait(t, k->cb_wait_type, cond_id));
    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, gate));
    const int on_gate = test_in_wait(t, WAIT_TYPE_SEMA, gate);
    const int deleted = k->delete_cond();
    const SceUInt64 gate_time = sceKernelGetProcessTimeWide();
    sceKernelSignalSema(gate, 1);
    join_t(t);
    const SceInt64 elapsed = (SceInt64)(t_end - gate_time);

    PRINTF(on_gate, "%s, %s: T's callback waits on the gate", k->name, name);
    PRINTF(deleted, "%s, %s: the delete", k->name, name);
    PRINTF(t_result, "%s, %s: T's wait", k->name, name);
    PRINTF(elapsed < SOON_US, "%s, %s: T's wait returned within 1 s of the gate", k->name, name);
    PRINTF(t_unlock, "%s, %s: T's unlock", k->name, name);
    PRINTF(callback_runs, "%s, %s: runs of the callback", k->name, name);
    teardown();
}

// Heavy only. The callback waits on the gate while the main thread deletes M, then the gate opens.
// The delete doesn't end T's wait, so T is back in C's queue, and the main thread checks whether T
// returns within 1 s of the gate before it signals C.
static void delete_mutex_case(void) {
    const Kind *const k = &kinds[0];
    const char *const name = "delete the mutex";
    setup(k, CB_GATE, WAIT_US);
    const SceUID t = start_t();
    TEST_WAIT_FOR(callback_ready && test_in_wait(t, k->cb_wait_type, cond_id));
    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, gate));
    const int on_gate = test_in_wait(t, WAIT_TYPE_SEMA, gate);
    const int deleted = sceKernelDeleteMutex(mutex);
    sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(test_in_wait(t, k->cb_wait_type, cond_id));
    const int ended_soon = wait_end(t, SOON_US);
    const int signal_result = k->signal();
    if (!ended_soon)
        join_t(t);

    PRINTF(on_gate, "%s, %s: T's callback waits on the gate", k->name, name);
    PRINTF(deleted, "%s, %s: the delete", k->name, name);
    PRINTF(ended_soon, "%s, %s: T's wait returned within 1 s of the gate", k->name, name);
    PRINTF(signal_result, "%s, %s: the signal", k->name, name);
    PRINTF(t_result, "%s, %s: T's wait", k->name, name);
    PRINTF(t_unlock, "%s, %s: T's unlock", k->name, name);
    PRINTF(callback_runs, "%s, %s: runs of the callback", k->name, name);
    teardown();
}

// Heavy only, the second run. The callback waits on the gate while the main thread deletes M, then
// the gate opens, and nobody signals C. T's wait has a timeout of 2 s, so it must last that long.
static void delete_mutex_timeout_case(void) {
    const Kind *const k = &kinds[0];
    const char *const name = "delete the mutex, then the timeout";
    setup(k, CB_GATE, SHORT_WAIT_US);
    const SceUID t = start_t();
    TEST_WAIT_FOR(callback_ready && test_in_wait(t, k->cb_wait_type, cond_id));
    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, gate));
    const int on_gate = test_in_wait(t, WAIT_TYPE_SEMA, gate);
    const int deleted = sceKernelDeleteMutex(mutex);
    sceKernelSignalSema(gate, 1);
    join_t(t);
    const SceInt64 elapsed = (SceInt64)(t_end - t_start);

    PRINTF(on_gate, "%s, %s: T's callback waits on the gate", k->name, name);
    PRINTF(deleted, "%s, %s: the delete", k->name, name);
    PRINTF(t_result, "%s, %s: T's wait", k->name, name);
    PRINTF(elapsed >= LATE_US, "%s, %s: T's wait lasted at least 1.75 s", k->name, name);
    PRINTF(t_unlock, "%s, %s: T's unlock", k->name, name);
    PRINTF(callback_runs, "%s, %s: runs of the callback", k->name, name);
    teardown();
}

// The callback deletes the condition, without a gate. Checks whether T's wait returns within 1 s
// of the notify.
static void callback_delete_case(const Kind *k) {
    const char *const name = "the callback deletes the condition";
    setup(k, CB_DELETE_COND, WAIT_US);
    const SceUID t = start_t();
    TEST_WAIT_FOR(callback_ready && test_in_wait(t, k->cb_wait_type, cond_id));
    const SceUInt64 notify_time = sceKernelGetProcessTimeWide();
    sceKernelNotifyCallback(callback, 1);
    join_t(t);
    const SceInt64 elapsed = (SceInt64)(t_end - notify_time);

    PRINTF(delete_in_callback, "%s, %s: the callback's delete", k->name, name);
    PRINTF(t_result, "%s, %s: T's wait", k->name, name);
    PRINTF(elapsed < SOON_US, "%s, %s: T's wait returned within 1 s of the notify", k->name, name);
    PRINTF(t_unlock, "%s, %s: T's unlock", k->name, name);
    PRINTF(callback_runs, "%s, %s: runs of the callback", k->name, name);
    teardown();
}

// Heavy only. The main thread holds M and signals C, so T waits to lock M again with callbacks.
// The main thread notifies T's callback, which waits on the gate. The main thread deletes M, then
// opens the gate, and checks whether T's wait returns within 1 s of the gate.
static void relock_delete_mutex_case(void) {
    const Kind *const k = &kinds[0];
    const char *const name = "delete the mutex in the relock";
    setup(k, CB_GATE, WAIT_US);
    const SceUID t = start_t();
    TEST_WAIT_FOR(callback_ready && test_in_wait(t, k->cb_wait_type, cond_id));
    k->lock();
    const int signal_result = k->signal();
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_COND_MUTEX | WAIT_TYPE_CB, mutex));
    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, gate));
    const int on_gate = test_in_wait(t, WAIT_TYPE_SEMA, gate);
    const int deleted = sceKernelDeleteMutex(mutex);
    const SceUInt64 gate_time = sceKernelGetProcessTimeWide();
    sceKernelSignalSema(gate, 1);
    join_t(t);
    const SceInt64 elapsed = (SceInt64)(t_end - gate_time);

    PRINTF(signal_result, "%s, %s: the signal", k->name, name);
    PRINTF(on_gate, "%s, %s: T's callback waits on the gate", k->name, name);
    PRINTF(deleted, "%s, %s: the delete", k->name, name);
    PRINTF(t_result, "%s, %s: T's wait", k->name, name);
    PRINTF(elapsed < SOON_US, "%s, %s: T's wait returned within 1 s of the gate", k->name, name);
    PRINTF(t_unlock, "%s, %s: T's unlock", k->name, name);
    PRINTF(callback_runs, "%s, %s: runs of the callback", k->name, name);
    teardown();
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    delete_cond_case(&kinds[0]);
    delete_cond_case(&kinds[1]);
    delete_mutex_case();
    delete_mutex_timeout_case();
    callback_delete_case(&kinds[0]);
    callback_delete_case(&kinds[1]);
    relock_delete_mutex_case();

    test_exit();
    return 0;
}
