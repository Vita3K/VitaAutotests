// Whether a callback notified after a condition variable waiter's wait ended, but before the waiter
// ran again, runs before that wait returns or stays pending, for the heavy and the lightweight
// condition variable. cond_callback_after_wake covers the heavy one for a signal, a mutex handed over
// and a delete, so this adds the lightweight one and the timeout. Every thread is pinned to core 0.
// The waiter T runs at 0xB0, below the main thread at 0x60, so a wait the main thread ends leaves T
// ready but not running. T locks the mutex and waits on the condition variable with callbacks, with
// a timeout of 300 ms in the timeout case and 4 s in the others. Every case has fresh objects.
// The main thread ends the wait by a signal, by a delete of the condition variable, or by busy
// waiting until the timeout passed (sleeping would let T run). Then it notifies T's callback and
// opens a gate, which holds T's steps after its wait until then. The cases, each for the heavy and
// the lightweight condition variable:
// - a signal
// - the timeout passes
// - a delete of the condition variable
// Each case prints T's status after the main thread ended the wait and after the notify, the call
// that ended the wait (not for the timeout), the notify, T's wait result, how often the callback ran
// when the wait returned, T's unlock of the mutex, the result of the sceKernelCheckCallback T calls
// next, and how often the callback ran after it.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define T_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define WAIT_US (2 * TEST_WAIT_US)
#define TIMEOUT_US 300000
#define BUSY_US 1000000

typedef enum Variant { HEAVY,
    LIGHT } Variant;
typedef enum Kind { SIGNAL,
    TIMEOUT,
    DELETE } Kind;

static SceUID mutex, cond, callback, gate;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static Variant variant;
static Kind kind;
static volatile int callback_ready, callback_runs, done;
static volatile int wait_result, runs_in_wait, unlock_result, check_result,
    runs_after_check;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    return 0;
}

static int lock_mutex(void) {
    return variant == HEAVY ? sceKernelLockMutex(mutex, 1, NULL)
                            : sceKernelLockLwMutex(&lw_mutex, 1, NULL);
}

static int unlock_mutex(void) {
    return variant == HEAVY ? sceKernelUnlockMutex(mutex, 1)
                            : sceKernelUnlockLwMutex(&lw_mutex, 1);
}

static int wait_with_callbacks(SceUInt *timeout) {
    return variant == HEAVY ? sceKernelWaitCondCB(cond, timeout)
                            : sceKernelWaitLwCondCB(&lw_cond, timeout);
}

static int signal_cond(void) {
    return variant == HEAVY ? sceKernelSignalCond(cond)
                            : sceKernelSignalLwCond(&lw_cond);
}

static int delete_cond(void) {
    return variant == HEAVY ? sceKernelDeleteCond(cond)
                            : sceKernelDeleteLwCond(&lw_cond);
}

// The wait type of a thread waiting on the condition variable, with the
// callback bit
static SceUInt32 cond_wait_type(void) {
    return (variant == HEAVY ? WAIT_TYPE_COND : WAIT_TYPE_LW_COND) | WAIT_TYPE_CB;
}

// Whether thread waits on the condition variable with callbacks
static int in_cond_wait(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && (info.status & SCE_THREAD_WAITING) && info.waitType == cond_wait_type();
}

// Busy waits until us microseconds have passed since start, so a timeout that passes meanwhile
// doesn't let the other thread run
static void busy_wait(SceUInt64 start_us, SceUInt64 us) {
    while (sceKernelGetProcessTimeWide() - start_us < us)
        ;
}

static int status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

// Locks the mutex, waits on the condition variable with callbacks and then runs
// its checks
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    lock_mutex();
    callback_ready = 1;
    SceUInt timeout = kind == TIMEOUT ? TIMEOUT_US : WAIT_US;
    wait_result = wait_with_callbacks(&timeout);
    // The main thread releases the gate after its notify, so the steps after the wait run after it
    SceUInt gate_timeout = WAIT_US;
    sceKernelWaitSema(gate, 1, &gate_timeout);
    runs_in_wait = callback_runs;
    unlock_result = unlock_mutex();
    check_result = sceKernelCheckCallback();
    runs_after_check = callback_runs;
    sceKernelDeleteCallback(callback);
    done = 1;
    return 0;
}

static SceUID start(SceKernelThreadEntry entry, int priority) {
    const SceUID thread = sceKernelCreateThread("t", entry, priority, 0x4000, 0,
        SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

static void create_objects(void) {
    if (variant == HEAVY) {
        mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
        cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    } else {
        sceKernelCreateLwMutex(&lw_mutex, "mutex", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "cond", 0, &lw_mutex, NULL);
    }
}

static void destroy_objects(void) {
    if (variant == HEAVY) {
        sceKernelDeleteMutex(mutex);
    } else {
        sceKernelDeleteLwMutex(&lw_mutex);
    }
}

static void run_case(const char *name, Variant v, Kind k) {
    variant = v;
    kind = k;
    callback_ready = callback_runs = done = 0;
    wait_result = runs_in_wait = unlock_result = check_result = runs_after_check = NOT_DONE;
    create_objects();
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);

    const SceUID t = start(t_entry, T_PRIORITY);
    TEST_WAIT_FOR(callback_ready && in_cond_wait(t));
    const SceUInt64 seen = sceKernelGetProcessTimeWide();
    int end = NOT_DONE;
    switch (k) {
    case SIGNAL:
        end = signal_cond();
        break;
    case DELETE:
        end = delete_cond();
        break;
    case TIMEOUT:
        busy_wait(seen, BUSY_US);
        break;
    }
    const int status_end = status_of(t);
    const int notify = sceKernelNotifyCallback(callback, 1);
    const int status_notify = status_of(t);
    sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(done);
    test_join_thread(t);
    if (k != DELETE) {
        if (variant == HEAVY) {
            sceKernelDeleteCond(cond);
        } else {
            sceKernelDeleteLwCond(&lw_cond);
        }
    }
    destroy_objects();
    sceKernelDeleteSema(gate);

    if (k != TIMEOUT)
        PRINTF(end, "%s: the call that ended the wait", name);
    PRINTF(status_end, "%s: T's status after it", name);
    PRINTF(notify, "%s: the notify", name);
    PRINTF(status_notify, "%s: T's status after the notify", name);
    PRINTF(wait_result, "%s: T's wait", name);
    PRINTF(runs_in_wait, "%s: runs of the callback when the wait returned", name);
    PRINTF(unlock_result, "%s: T's unlock", name);
    PRINTF(check_result, "%s: T's check", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("heavy, signal, then notify", HEAVY, SIGNAL);
    run_case("heavy, timeout, then notify", HEAVY, TIMEOUT);
    run_case("heavy, condition deleted, then notify", HEAVY, DELETE);
    run_case("light, signal, then notify", LIGHT, SIGNAL);
    run_case("light, timeout, then notify", LIGHT, TIMEOUT);
    run_case("light, condition deleted, then notify", LIGHT, DELETE);

    test_exit();
    return 0;
}
