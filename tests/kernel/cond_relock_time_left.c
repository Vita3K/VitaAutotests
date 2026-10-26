// The time left a condition variable wait with a timeout reports, when the wait has to take its
// mutex back before it returns. cond_semantics and lw_cond_semantics cover a timeout that passes
// while the mutex is held, and print only that the time left is 0. This adds the time left itself,
// for a signal that is followed by a relock, and the moment the wait returns. The cases, each for
// the heavy and the lightweight condition variable:
// - a wait with a timeout of 2 s, signaled at 200 ms while the main thread holds the mutex until
//   800 ms. The wait returns once the mutex is free, so the time left is that of the signal, about
//   1.8 s, or that of the relock, about 1.2 s. It prints the result, and whether the time left is
//   above 1.5 s, which is 1 for the time left of the signal and 0 for the time left of the relock.
// - a wait with a timeout of 200 ms, while the main thread holds the mutex from the wait's start
//   until 800 ms. The timeout passes, the wait waits for the mutex, and returns once the mutex is
//   free. It prints the result, the time left, whether the waiter was done before the mutex was free
//   (0), and whether the wait returned after the mutex was free (1, the wait can't return before).
// The main thread waits until the waiter waits on the condition variable, so the waiter has let go
// of the mutex and the main thread can lock it. Every case has fresh objects. The sleeps are time
// passing with wide margins, not waits for a thread to get somewhere.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define SIGNAL_US 200000
#define HOLD_US 800000
#define SIGNAL_HOLD_US 600000
#define WAIT_SIGNAL_US 2000000
#define WAIT_TIMEOUT_US 200000
#define MID_LEFT_US 1500000

typedef enum Variant { HEAVY,
    LIGHT } Variant;

static SceUID mutex, cond;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static Variant variant;
static volatile int wait_result, done;
static volatile SceUInt timeout_left;
static volatile SceUInt64 waiter_end_us;

static int lock_mutex(void) {
    return variant == HEAVY ? sceKernelLockMutex(mutex, 1, NULL)
                            : sceKernelLockLwMutex(&lw_mutex, 1, NULL);
}

static int unlock_mutex(void) {
    return variant == HEAVY ? sceKernelUnlockMutex(mutex, 1)
                            : sceKernelUnlockLwMutex(&lw_mutex, 1);
}

static int wait_cond(SceUInt *timeout) {
    return variant == HEAVY ? sceKernelWaitCondCB(cond, timeout)
                            : sceKernelWaitLwCondCB(&lw_cond, timeout);
}

static int signal_cond(void) {
    return variant == HEAVY ? sceKernelSignalCond(cond)
                            : sceKernelSignalLwCond(&lw_cond);
}

// The wait type of a thread waiting on the condition variable, with the
// callback bit
static SceUInt32 cond_wait_type(void) {
    return variant == HEAVY ? WAIT_TYPE_COND : WAIT_TYPE_LW_COND;
}

// Whether thread waits on the condition variable
static int in_cond_wait(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && (info.status & SCE_THREAD_WAITING) && (info.waitType & ~WAIT_TYPE_CB) == cond_wait_type();
}

// Locks the mutex, waits on the condition variable with the timeout the
// argument holds, and records the result, the time left and when it returned
static int waiter(SceSize args, void *argp) {
    (void)args;
    const SceUInt timeout_start = *(const SceUInt *)argp;
    lock_mutex();
    SceUInt timeout = timeout_start;
    wait_result = wait_cond(&timeout);
    timeout_left = timeout;
    waiter_end_us = sceKernelGetProcessTimeWide();
    unlock_mutex();
    done = 1;
    return 0;
}

static SceUID start_waiter(SceUInt timeout) {
    wait_result = 0xDEAD;
    timeout_left = 0xDEAD;
    done = 0;
    const SceUID thread = sceKernelCreateThread(
        "waiter", waiter, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    sceKernelStartThread(thread, sizeof(timeout), &timeout);
    TEST_WAIT_FOR(in_cond_wait(thread));
    return thread;
}

static void create(void) {
    if (variant == HEAVY) {
        mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
        cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    } else {
        sceKernelCreateLwMutex(&lw_mutex, "mutex", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "cond", 0, &lw_mutex, NULL);
    }
}

static void destroy(void) {
    if (variant == HEAVY) {
        sceKernelDeleteCond(cond);
        sceKernelDeleteMutex(mutex);
    } else {
        sceKernelDeleteLwCond(&lw_cond);
        sceKernelDeleteLwMutex(&lw_mutex);
    }
}

// The waiter is signaled while the main thread holds the mutex, which the
// waiter needs back
static void signal_then_relock_case(const char *name) {
    create();
    const SceUID thread = start_waiter(WAIT_SIGNAL_US);
    lock_mutex();
    sceKernelDelayThread(SIGNAL_US);
    signal_cond();
    sceKernelDelayThread(SIGNAL_HOLD_US);
    unlock_mutex();
    test_join_thread(thread);
    destroy();

    PRINTF(wait_result, "%s: the wait", name);
    PRINTF(timeout_left > MID_LEFT_US,
        "%s: the time left is above 1.5 s, the one of the signal", name);
}

// The timeout passes while the main thread holds the mutex, and the waiter
// waits for it to be free
static void timeout_with_mutex_held_case(const char *name) {
    create();
    const SceUID thread = start_waiter(WAIT_TIMEOUT_US);
    lock_mutex();
    sceKernelDelayThread(HOLD_US);
    const SceUInt64 unlock_us = sceKernelGetProcessTimeWide();
    const int done_before_unlock = done;
    unlock_mutex();
    test_join_thread(thread);
    destroy();

    PRINTF(wait_result, "%s: the wait", name);
    PRINTF(timeout_left, "%s: the time left", name);
    PRINTF(done_before_unlock,
        "%s: the waiter was done before the mutex was free", name);
    PRINTF(waiter_end_us >= unlock_us,
        "%s: the wait returned after the mutex was free", name);
}

int main(void) {
    variant = HEAVY;
    signal_then_relock_case("heavy, signal then relock");
    timeout_with_mutex_held_case("heavy, timeout with the mutex held");
    variant = LIGHT;
    signal_then_relock_case("light, signal then relock");
    timeout_with_mutex_held_case("light, timeout with the mutex held");

    test_exit();
    return 0;
}
