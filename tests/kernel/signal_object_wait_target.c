// A thread signal sent to a thread that waits on a semaphore leaves that wait as it was. Each case
// has a fresh semaphore that orders its waiters by priority and starts with a count of 0, and fresh
// threads. The waiter W waits for a count of 1 with a timeout of 4 seconds. Every thread runs on
// core 0. The main thread runs at 0x40, above every waiter, so a waiter that becomes ready runs only
// once the main thread blocks. The main thread starts each waiter and polls until it waits.
//
// Priority change: waiter A at priority 0x50 waits first, then W at 0x60 waits behind it. The main
// thread sends W a signal and right away, with no call that can block in between, reads W's
// status, its wait type, whether it waits on the semaphore, and the semaphore's count of waiting
// threads. Then it raises W to 0x48, still below the main thread, signals the semaphore once and
// right away reads that count again. Once one waiter has its count, it signals once more so the
// other ends too. Each waiter records its result and the order it ended in, and the test prints
// whether W got the first count.
//
// Callback: W at 0x60 creates a callback and waits with sceKernelWaitSemaCB. The main thread sends
// W a signal and reads the same four values right away. Then it notifies the callback. The
// callback records the semaphore's count of waiting threads and W's status while it runs on W. The
// semaphore still has no count, so W waits on afterwards. Once the callback ran and W waits again,
// the main thread reads W's status, its wait type, whether that has the callback bit 0x80000000,
// whether W waits on the semaphore, and the semaphore's count of waiting threads. Then it signals
// the semaphore so W ends.
//
// In both cases W then calls sceKernelWaitSignal with a timeout of 1 second, and records its result
// and whether it returned within 500 ms, which shows whether the signal is still pending. The
// prints come last, since a print blocks the main thread and lets the waiters run.
//
// On a PS Vita TV on firmware 3.65 the signal doesn't touch the semaphore wait. Right after it W is
// still WAITING (0x8) on the semaphore with wait type 0x20, or 0x80000020 for the wait with
// callbacks, and the semaphore counts both waiters, or W alone. Raising W moves it ahead of A, so W
// gets the first count, and both waits return 0. The notify ends the wait with callbacks: while the
// callback runs the semaphore counts no waiter and W is RUNNING (0x1), and afterwards W waits on
// the semaphore again with wait type 0x80000020. The signal stays pending, so the later
// sceKernelWaitSignal returns 0 at once.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x40
#define A_PRIORITY 0x50
#define W_PRIORITY 0x60
#define RAISED 0x48
#define NOT_DONE 0xDEAD
#define SIGNAL_WAIT_US 1000000
#define AT_ONCE_US 500000

static SceUID sema, callback, w;
static volatile int order;
static volatile int a_done, a_result, a_order;
static volatile int w_done, w_result, w_order, w_sig_result, w_sig_at_once;
static volatile int callback_ran, callback_waiters, callback_status;

struct WaitState {
    int status;
    int type;
    int on_sema;
    int waiters;
};

// The semaphore's count of waiting threads
static int sema_waiters(void) {
    SceKernelSemaInfo info = { .size = sizeof(info) };
    if (sceKernelGetSemaInfo(sema, &info) != 0)
        return NOT_DONE;
    return info.numWaitThreads;
}

static int thread_status(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    if (sceKernelGetThreadInfo(thread, &info) != 0)
        return NOT_DONE;
    return info.status;
}

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_waiters = sema_waiters();
    callback_status = thread_status(w);
    callback_ran = 1;
    return 0;
}

// Waits for the signal that should still be pending
static void wait_signal(void) {
    SceUInt32 timeout = SIGNAL_WAIT_US;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    w_sig_result = sceKernelWaitSignal(0, 0, &timeout);
    w_sig_at_once = sceKernelGetProcessTimeWide() - start < AT_ONCE_US;
}

static int a_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    a_result = sceKernelWaitSema(sema, 1, &timeout);
    a_order = ++order;
    a_done = 1;
    return 0;
}

static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    w_result = sceKernelWaitSema(sema, 1, &timeout);
    w_order = ++order;
    w_done = 1;
    wait_signal();
    return 0;
}

static int w_cb_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    SceUInt timeout = 2 * TEST_WAIT_US;
    w_result = sceKernelWaitSemaCB(sema, 1, &timeout);
    w_done = 1;
    wait_signal();
    sceKernelDeleteCallback(callback);
    return 0;
}

static SceUID start(SceKernelThreadEntry entry, int priority) {
    const SceUID thread = sceKernelCreateThread("waiter", entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    test_wait_until_waiting(thread);
    return thread;
}

// Reads W's thread info, then the semaphore's info, with no call that can block
static struct WaitState read_state(SceUID thread) {
    struct WaitState state = { NOT_DONE, NOT_DONE, NOT_DONE, NOT_DONE };
    SceKernelThreadInfo info = { .size = sizeof(info) };
    if (sceKernelGetThreadInfo(thread, &info) == 0) {
        state.status = info.status;
        state.type = info.waitType;
        state.on_sema = info.waitId == sema;
    }
    state.waiters = sema_waiters();
    return state;
}

static int is_waiting(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && (info.status & SCE_THREAD_WAITING);
}

static void reset(void) {
    order = 0;
    a_done = w_done = callback_ran = 0;
    a_result = a_order = w_result = w_order = w_sig_result = w_sig_at_once = NOT_DONE;
    callback_waiters = callback_status = NOT_DONE;
    sema = sceKernelCreateSema("sema", SCE_KERNEL_ATTR_THREAD_PRIO, 0, 2, NULL);
}

static void print_state(const char *name, const char *when, const struct WaitState *state) {
    PRINTF(state->status, "%s: W's status %s", name, when);
    PRINTF(state->type, "%s: W's wait type %s", name, when);
    PRINTF(state->on_sema, "%s: W waits on the semaphore %s", name, when);
    PRINTF(state->waiters, "%s: the semaphore's waiting threads %s", name, when);
}

static void print_signal_wait(const char *name) {
    PRINTF(w_sig_result, "%s: W's wait for the signal after it", name);
    PRINTF(w_sig_at_once, "%s: that wait returned at once", name);
}

static void priority_case(void) {
    const char *name = "priority change";
    reset();
    const SceUID a = start(a_entry, A_PRIORITY);
    w = start(w_entry, W_PRIORITY);

    const int sent = sceKernelSendSignal(w);
    const struct WaitState state = read_state(w);
    const int changed = sceKernelChangeThreadPriority(w, RAISED);
    sceKernelSignalSema(sema, 1);
    const int waiters = sema_waiters();
    TEST_WAIT_FOR(a_done || w_done);
    sceKernelSignalSema(sema, 1);
    test_join_thread(a);
    test_join_thread(w);
    sceKernelDeleteSema(sema);

    PRINTF(sent, "%s: the signal to W", name);
    print_state(name, "after the signal", &state);
    PRINTF(changed, "%s: raising W", name);
    PRINTF(waiters, "%s: the semaphore's waiting threads after the first count", name);
    PRINTF(w_order == 1, "%s: W got the first count", name);
    PRINTF(a_result, "%s: A's wait", name);
    PRINTF(w_result, "%s: W's wait", name);
    print_signal_wait(name);
}

static void callback_case(void) {
    const char *name = "callback";
    reset();
    w = start(w_cb_entry, W_PRIORITY);

    const int sent = sceKernelSendSignal(w);
    const struct WaitState state = read_state(w);
    const int notified = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(callback_ran && (w_done || is_waiting(w)));

    // W is blocked until the semaphore gets a count, so this read sees its state
    const int ran = callback_ran;
    const struct WaitState again = read_state(w);
    sceKernelSignalSema(sema, 1);
    test_join_thread(w);
    sceKernelDeleteSema(sema);

    PRINTF(sent, "%s: the signal to W", name);
    print_state(name, "after the signal", &state);
    PRINTF(notified, "%s: the notify", name);
    PRINTF(ran, "%s: the callback ran", name);
    PRINTF(callback_waiters, "%s: the semaphore's waiting threads in the callback", name);
    PRINTF(callback_status, "%s: W's status in the callback", name);
    print_state(name, "after the callback", &again);
    PRINTF((again.type & WAIT_TYPE_CB) != 0, "%s: that wait type has the callback bit", name);
    PRINTF(w_result, "%s: W's wait", name);
    print_signal_wait(name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);

    priority_case();
    callback_case();

    test_exit();
    return 0;
}
