// What a condition variable waiter gets when its timeout ends its wait before the waiter can run
// again, and what happens to it next. A waiter W locks a mutex M and waits on a condition C with a
// timeout of 300 ms, once for a heavy condition (sceKernelCreateCond) and once for a lightweight
// one (sceKernelCreateLwCond). The main thread runs on core 0 at 0x60 and W is pinned to core 0 at
// 0xB0, so W doesn't run while the main thread keeps going. The main thread polls the thread info
// with no delay until W is ready with no wait type, which is the state the timeout leaves W in, for
// at most 5 s. Then it reads W's status and wait type, the waiters of C and M, and whether W owns
// M, and only then acts, on fresh objects. The cases:
// - delete C, then block so that W runs. Prints the delete's result, W's wait result, whether W
//   owns M after it, W's unlock result and whether W's time left is 0
// - heavy only: delete M, then block. Prints the delete's result, W's wait result and W's unlock
//   result
// - signal C, signal all of C and signal W by its thread ID, then block. Prints the three results,
//   W's wait result, whether W owns M after it and W's unlock result
// The lightweight mutex is never deleted, so each lightweight case gets a mutex of its own.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define TIMEOUT_US 300000
#define NOT_SET 0xDEAD
#define POLL_US 5000000

// One kind of condition variable, with the calls that differ
typedef struct Kind {
    const char *name;
    SceUInt32 cond_wait_type;
    void (*create)(void);
    void (*destroy)(void);
    int (*lock)(void);
    int (*unlock)(void);
    int (*wait)(SceUInt *timeout);
    int (*signal)(void);
    int (*signal_all)(void);
    int (*signal_to)(SceUID thread);
    int (*delete_cond)(void);
    int (*cond_waiters)(void);
    int (*mutex_waiters)(void);
    SceUID (*owner)(void);
} Kind;

static SceUID mutex, cond;
static SceKernelLwMutexWork lw_mutexes[4];
static int lw_mutexes_used;
static SceKernelLwMutexWork *lw_mutex;
static SceKernelLwCondWork lw_cond;
// The UID a wait on the condition reports, which the create calls store
static SceUID cond_id;
static const Kind *kind;
static volatile int w_result, w_holds, w_unlock, w_done;
static volatile SceUInt w_left;

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

static int heavy_wait(SceUInt *timeout) {
    return sceKernelWaitCond(cond, timeout);
}

static int heavy_signal(void) {
    return sceKernelSignalCond(cond);
}

static int heavy_signal_all(void) {
    return sceKernelSignalCondAll(cond);
}

static int heavy_signal_to(SceUID thread) {
    return sceKernelSignalCondTo(cond, thread);
}

static int heavy_delete_cond(void) {
    return sceKernelDeleteCond(cond);
}

static int heavy_cond_waiters(void) {
    SceKernelCondInfo info = { .size = sizeof(info) };
    if (sceKernelGetCondInfo(cond, &info) < 0)
        return -1;
    return info.numWaitThreads;
}

static int heavy_mutex_waiters(void) {
    MutexInfo info = { .size = sizeof(info) };
    if (sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info) < 0)
        return -1;
    return info.numWaitThreads;
}

static SceUID heavy_owner(void) {
    MutexInfo info = { .size = sizeof(info) };
    if (sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info) < 0)
        return 0;
    return info.currentOwnerId;
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

static int lw_wait(SceUInt *timeout) {
    return sceKernelWaitLwCond(&lw_cond, timeout);
}

static int lw_signal(void) {
    return sceKernelSignalLwCond(&lw_cond);
}

static int lw_signal_all(void) {
    return sceKernelSignalLwCondAll(&lw_cond);
}

static int lw_signal_to(SceUID thread) {
    return sceKernelSignalLwCondTo(&lw_cond, thread);
}

static int lw_delete_cond(void) {
    return sceKernelDeleteLwCond(&lw_cond);
}

static int lw_cond_waiters(void) {
    LwCondInfo info = { .size = sizeof(info) };
    if (sceKernelGetLwCondInfo(&lw_cond, &info) < 0)
        return -1;
    return info.numWaitThreads;
}

static int lw_mutex_waiters(void) {
    LwMutexInfo info = { .size = sizeof(info) };
    if (sceKernelGetLwMutexInfo(lw_mutex, &info) < 0)
        return -1;
    return info.numWaitThreads;
}

static SceUID lw_owner(void) {
    LwMutexInfo info = { .size = sizeof(info) };
    if (sceKernelGetLwMutexInfo(lw_mutex, &info) < 0)
        return 0;
    return info.currentOwnerId;
}

static const Kind kinds[2] = {
    { "heavy", WAIT_TYPE_COND, heavy_create, heavy_destroy, heavy_lock, heavy_unlock, heavy_wait,
        heavy_signal, heavy_signal_all, heavy_signal_to, heavy_delete_cond, heavy_cond_waiters,
        heavy_mutex_waiters, heavy_owner },
    { "lightweight", WAIT_TYPE_LW_COND, lw_create, lw_destroy, lw_lock, lw_unlock, lw_wait,
        lw_signal, lw_signal_all, lw_signal_to, lw_delete_cond, lw_cond_waiters, lw_mutex_waiters,
        lw_owner },
};

// Locks the mutex, waits on the condition with the timeout, then records the result, the time left,
// whether W owns the mutex and the result of an unlock
static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    kind->lock();
    SceUInt timeout = TIMEOUT_US;
    w_result = kind->wait(&timeout);
    w_left = timeout;
    w_holds = kind->owner() == sceKernelGetThreadId();
    w_unlock = kind->unlock();
    w_done = 1;
    return 0;
}

// Returns whether thread is ready and in no wait, which is the state a timeout leaves a waiter in
static int is_claimed(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.status == SCE_THREAD_READY &&
        info.waitType == 0;
}

// Creates the objects and W, and waits until W waits on the condition
static SceUID start_waiter(const Kind *k) {
    kind = k;
    w_result = w_holds = w_unlock = w_left = NOT_SET;
    w_done = 0;
    k->create();
    const SceUID w = sceKernelCreateThread(
        "waiter", w_entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(w, k->cond_wait_type, cond_id));
    return w;
}

// Polls the thread info of w with no delay until w is claimed, for at most 5 s
static void poll_claimed(SceUID w) {
    const SceUInt64 end = sceKernelGetProcessTimeWide() + POLL_US;
    while (sceKernelGetProcessTimeWide() < end && !is_claimed(w))
        ;
}

// What the main thread reads once W's timeout has passed
typedef struct Claimed {
    SceUInt32 status;
    SceUInt32 wait_type;
    int cond_waiters;
    int mutex_waiters;
    int owns;
} Claimed;

static Claimed read_claimed(SceUID w) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    Claimed c;
    sceKernelGetThreadInfo(w, &info);
    c.status = info.status;
    c.wait_type = info.waitType;
    c.cond_waiters = kind->cond_waiters();
    c.mutex_waiters = kind->mutex_waiters();
    c.owns = kind->owner() == w;
    return c;
}

static void print_claimed(const char *name, const Claimed *c) {
    PRINTF(c->status, "%s, %s: W's status once its timeout passed", kind->name, name);
    PRINTF(c->wait_type, "%s, %s: W's wait type once its timeout passed", kind->name, name);
    PRINTF(c->cond_waiters, "%s, %s: the condition's waiters", kind->name, name);
    PRINTF(c->mutex_waiters, "%s, %s: the mutex's waiters", kind->name, name);
    PRINTF(c->owns, "%s, %s: W owns the mutex once its timeout passed", kind->name, name);
}

// Waits until W has recorded its results, then joins it. A W that is still blocked after that is
// left as it is.
static void end_waiter(SceUID w) {
    TEST_WAIT_FOR(w_done);
    SceUInt timeout = TEST_WAIT_US;
    if (w_done && sceKernelWaitThreadEnd(w, NULL, &timeout) == 0)
        sceKernelDeleteThread(w);
}

// The main thread deletes the condition once W is claimed, then blocks so W runs
static void delete_cond_case(const Kind *k) {
    const char *const name = "delete the condition";
    const SceUID w = start_waiter(k);
    poll_claimed(w);
    const Claimed claimed = read_claimed(w);
    const int deleted = k->delete_cond();
    end_waiter(w);
    print_claimed(name, &claimed);
    PRINTF(deleted, "%s, %s: the delete", k->name, name);
    PRINTF(w_result, "%s, %s: W's result", k->name, name);
    PRINTF(w_holds, "%s, %s: W owns the mutex after it", k->name, name);
    PRINTF(w_unlock, "%s, %s: W's unlock after it", k->name, name);
    PRINTF(w_left == 0, "%s, %s: W's time left is 0", k->name, name);
    k->destroy();
}

// The main thread deletes the heavy mutex once W is claimed, then blocks so W runs
static void delete_mutex_case(void) {
    const Kind *const k = &kinds[0];
    const char *const name = "delete the mutex";
    const SceUID w = start_waiter(k);
    poll_claimed(w);
    const Claimed claimed = read_claimed(w);
    const int deleted = sceKernelDeleteMutex(mutex);
    end_waiter(w);
    print_claimed(name, &claimed);
    PRINTF(deleted, "%s, %s: the delete", k->name, name);
    PRINTF(w_result, "%s, %s: W's result", k->name, name);
    PRINTF(w_unlock, "%s, %s: W's unlock after it", k->name, name);
    k->destroy();
}

// The main thread signals the condition, signals all of it and signals W, then blocks so W runs
static void signal_case(const Kind *k) {
    const char *const name = "signal, signal all, then signal W";
    const SceUID w = start_waiter(k);
    poll_claimed(w);
    const Claimed claimed = read_claimed(w);
    const int signal_result = k->signal();
    const int signal_all_result = k->signal_all();
    const int signal_to_result = k->signal_to(w);
    end_waiter(w);
    print_claimed(name, &claimed);
    PRINTF(signal_result, "%s, %s: the signal", k->name, name);
    PRINTF(signal_all_result, "%s, %s: the signal to all", k->name, name);
    PRINTF(signal_to_result, "%s, %s: the signal to W", k->name, name);
    PRINTF(w_result, "%s, %s: W's result", k->name, name);
    PRINTF(w_holds, "%s, %s: W owns the mutex after it", k->name, name);
    PRINTF(w_unlock, "%s, %s: W's unlock after it", k->name, name);
    k->destroy();
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    delete_cond_case(&kinds[0]);
    delete_cond_case(&kinds[1]);
    delete_mutex_case();
    signal_case(&kinds[0]);
    signal_case(&kinds[1]);

    test_exit();
    return 0;
}
