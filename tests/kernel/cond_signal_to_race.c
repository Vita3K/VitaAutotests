// Which party ends a condition variable wait when its timeout, a callback notify and a signal to
// the waiter race. The claim rule says exactly one of them ends the wait, so a signal that returns
// 0 ends the wait with 0 while the mutex is owned, a signal that returns 0x80028022 (the waiter
// isn't waiting on the condition variable) never comes with a wait that returns 0, and a wait that
// returns 0 or WAIT_TIMEOUT owns the mutex. The waiter runs on core 0 with a timeout of 20 ms, and
// the main thread runs on core 1 as the signaler. In each round the main thread waits until the
// waiter is about to wait, then busy waits to an offset from the waiter's start, which sweeps from
// before the deadline to after it over 100 rounds, and signals the waiter. Race one is the timeout
// against the signal. Race two is a callback notify against the signal, with the notify first in
// even rounds and after the signal in odd rounds. Each race runs for a heavy and a lightweight
// condition variable. The output is the round count and the violation count of each race, which
// don't depend on which party wins, so they are the same on every run.
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define ROUNDS 100
#define WAITER_PRIORITY 0xB0
#define TIMEOUT_US 20000
// The offsets sweep from 0 to twice the timeout
#define SWEEP_US (2 * TIMEOUT_US)
// What SignalCondTo returns for a thread that isn't waiting on the condition variable
#define NOT_WAITING ((int)SCE_KERNEL_ERROR_ILLEGAL_THREAD_ID)
#define WAIT_TIMEOUT ((int)SCE_KERNEL_ERROR_WAIT_TIMEOUT)

typedef enum Race { TIMEOUT_RACE, NOTIFY_RACE, RACE_COUNT } Race;

static const char *const race_names[RACE_COUNT] = {
    "timeout against the signal",
    "callback notify against the signal",
};

// One kind of condition variable, with the calls that differ
typedef struct Kind {
    const char *name;
    void (*create)(void);
    void (*destroy)(void);
    int (*lock)(SceUInt *timeout);
    int (*unlock)(void);
    int (*wait)(SceUInt *timeout, int with_callbacks);
    int (*signal_to)(SceUID thread);
} Kind;

static SceUID mutex, cond;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;

static void heavy_create(void) {
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
}

static void heavy_destroy(void) {
    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(mutex);
}

static int heavy_lock(SceUInt *timeout) {
    return sceKernelLockMutex(mutex, 1, timeout);
}

static int heavy_unlock(void) {
    return sceKernelUnlockMutex(mutex, 1);
}

static int heavy_wait(SceUInt *timeout, int with_callbacks) {
    return with_callbacks ? sceKernelWaitCondCB(cond, timeout) : sceKernelWaitCond(cond, timeout);
}

static int heavy_signal_to(SceUID thread) {
    return sceKernelSignalCondTo(cond, thread);
}

static void lw_create(void) {
    sceKernelCreateLwMutex(&lw_mutex, "mutex", 0, 0, NULL);
    sceKernelCreateLwCond(&lw_cond, "cond", 0, &lw_mutex, NULL);
}

// The lightweight mutex isn't deleted here
static void lw_destroy(void) {
    sceKernelDeleteLwCond(&lw_cond);
}

static int lw_lock(SceUInt *timeout) {
    return sceKernelLockLwMutex(&lw_mutex, 1, timeout);
}

static int lw_unlock(void) {
    return sceKernelUnlockLwMutex(&lw_mutex, 1);
}

static int lw_wait(SceUInt *timeout, int with_callbacks) {
    return with_callbacks ? sceKernelWaitLwCondCB(&lw_cond, timeout)
                          : sceKernelWaitLwCond(&lw_cond, timeout);
}

static int lw_signal_to(SceUID thread) {
    return sceKernelSignalLwCondTo(&lw_cond, thread);
}

static const Kind heavy = { "heavy", heavy_create, heavy_destroy, heavy_lock, heavy_unlock, heavy_wait,
    heavy_signal_to };
static const Kind lightweight = { "lightweight", lw_create, lw_destroy, lw_lock, lw_unlock, lw_wait,
    lw_signal_to };

static const Kind *kind;
// Set per round: whether the waiter runs callbacks, its callback, and what it reported
static int with_callbacks;
static SceUID callback;
static volatile int ready, wait_result, owned;
static volatile SceUInt64 start_us;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    return 0;
}

// Locks the mutex, publishes its start time, and waits with a timeout of 20 ms, then records
// whether it owns the mutex after the wait
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    if (with_callbacks)
        callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    SceUInt lock_timeout = TEST_WAIT_US;
    kind->lock(&lock_timeout);
    SceUInt timeout = TIMEOUT_US;
    start_us = sceKernelGetProcessTimeWide();
    ready = 1;
    wait_result = kind->wait(&timeout, with_callbacks);
    owned = kind->unlock() == 0;
    if (with_callbacks)
        sceKernelDeleteCallback(callback);
    return 0;
}

// Whether a signal and a wait ended the way the claim rule forbids
static int violated(int signaled, int result, int is_owned) {
    if (signaled == 0 && result != 0)
        return 1;
    if (signaled == NOT_WAITING && result == 0)
        return 1;
    if ((result == 0 || result == WAIT_TIMEOUT) && !is_owned)
        return 1;
    return 0;
}

// Runs one round of race, and returns 1 if it broke the claim rule
static int run_round(int round, Race race) {
    with_callbacks = race == NOTIFY_RACE;
    ready = 0;
    wait_result = 0xDEAD;
    owned = 0;
    const SceUID thread = sceKernelCreateThread("waiter", waiter, WAITER_PRIORITY, 0x4000, 0,
        SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    TEST_WAIT_FOR(ready);
    const SceUInt64 offset = (SceUInt64)round * SWEEP_US / ROUNDS;
    while (sceKernelGetProcessTimeWide() - start_us < offset)
        ;
    const int notify_first = race == NOTIFY_RACE && (round & 1) == 0;
    if (notify_first)
        sceKernelNotifyCallback(callback, 1);
    const int signaled = kind->signal_to(thread);
    if (race == NOTIFY_RACE && !notify_first)
        sceKernelNotifyCallback(callback, 1);
    test_join_thread(thread);
    return violated(signaled, wait_result, owned);
}

static void run_race(const Kind *k, Race race) {
    kind = k;
    k->create();
    int rounds = 0, violations = 0;
    for (int round = 0; round < ROUNDS; round++) {
        violations += run_round(round, race);
        rounds++;
    }
    k->destroy();
    PRINTF(rounds, "%s, %s: rounds", k->name, race_names[race]);
    PRINTF(violations, "%s, %s: violations", k->name, race_names[race]);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    run_race(&heavy, TIMEOUT_RACE);
    run_race(&heavy, NOTIFY_RACE);
    run_race(&lightweight, TIMEOUT_RACE);
    run_race(&lightweight, NOTIFY_RACE);

    test_exit();
    return 0;
}
