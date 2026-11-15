// What happens to a semaphore waiter queued behind one that needs more than the semaphore holds,
// once that first waiter stops waiting. Waiter A needs 3 and queues first, waiter B needs 1 and
// queues second, and the semaphore holds 2. First whether a signal reaches B at all, in a FIFO
// and a priority queue. Then A leaves the head: its timeout passes, its priority drops below B's in
// a priority queue, or it runs a callback during a wait with callbacks. Each case prints whether B
// got its count while A was gone, and the count left.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define HIGH_PRIORITY 0x80
#define LOW_PRIORITY 0x90

static SceUID sema, callback;
static volatile int result[2], done[2], in_callback, callback_done;
static volatile SceUInt timeouts[2];
static volatile int use_cb;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    in_callback = 1;
    sceKernelDelayThread(300000);
    callback_done = 1;
    return 0;
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = timeouts[i];
    if (i == 0 && use_cb) {
        callback = sceKernelCreateCallback("cb", 0, callback_entry, NULL);
        result[i] = sceKernelWaitSemaCB(sema, 3, &timeout);
        sceKernelDeleteCallback(callback);
    } else {
        result[i] = sceKernelWaitSema(sema, i == 0 ? 3 : 1, &timeout);
    }
    done[i] = 1;
    return 0;
}

static SceUID threads[2];

// Starts A and then B, each once the one before waits
static void start_waiters(SceUInt32 attr, int a_priority, int b_priority, SceUInt a_timeout, int cb) {
    sema = sceKernelCreateSema("sema", attr, 2, 10, NULL);
    use_cb = cb;
    in_callback = callback_done = 0;
    const int priorities[2] = { a_priority, b_priority };
    timeouts[0] = a_timeout;
    timeouts[1] = TEST_WAIT_US;
    for (int i = 0; i < 2; i++) {
        result[i] = 0xDEAD;
        done[i] = 0;
        threads[i] = sceKernelCreateThread("waiter", waiter, priorities[i], 0x4000, 0, 0, NULL);
        sceKernelStartThread(threads[i], sizeof(i), &i);
        test_wait_until_waiting(threads[i]);
    }
}

// Waits up to 500 ms for B to be done, for an outcome that would come at once
static int b_done_soon(void) {
    for (int i = 0; i < 500 && !done[1]; i++)
        sceKernelDelayThread(1000);
    return done[1];
}

static int count(void) {
    SceKernelSemaInfo info = { .size = sizeof(info) };
    sceKernelGetSemaInfo(sema, &info);
    return info.currentCount;
}

// Ends whatever still waits and deletes the semaphore
static void finish(void) {
    sceKernelDeleteSema(sema);
    for (int i = 0; i < 2; i++)
        test_join_thread(threads[i]);
}

static void signal_case(SceUInt32 attr, const char *name) {
    start_waiters(attr, HIGH_PRIORITY, LOW_PRIORITY, TEST_WAIT_US, 0);
    const int b_done = b_done_soon();
    PRINTF(b_done, "%s, B behind A with 2 there: B got its count", name);
    PRINTF(done[0], "%s, B behind A with 2 there: A is done", name);
    PRINTF(count(), "%s, B behind A with 2 there: the count", name);
    sceKernelSignalSema(sema, 1);
    TEST_WAIT_FOR(done[0] && done[1]);
    PRINTF(done[0], "%s, after a signal of 1: A is done", name);
    PRINTF(done[1], "%s, after a signal of 1: B is done", name);
    PRINTF(count(), "%s, after a signal of 1: the count", name);
    finish();
    PRINTF(result[0], "%s: A's result", name);
    PRINTF(result[1], "%s: B's result", name);
}

static void timeout_case(SceUInt32 attr, const char *name) {
    // Long enough that starting B never takes longer, and short of B's own timeout
    start_waiters(attr, HIGH_PRIORITY, LOW_PRIORITY, 1000000, 0);
    const int b_before = done[1];
    TEST_WAIT_FOR(done[0]);
    const int b_done = b_done_soon();
    PRINTF(b_before, "%s, before A's timeout: B is done", name);
    PRINTF(result[0], "%s: A's result", name);
    PRINTF(b_done, "%s, after A's timeout: B got its count", name);
    PRINTF(count(), "%s, after A's timeout: the count", name);
    finish();
    PRINTF(result[1], "%s: B's result", name);
}

static void priority_case(void) {
    start_waiters(SCE_KERNEL_ATTR_THREAD_PRIO, HIGH_PRIORITY, LOW_PRIORITY, TEST_WAIT_US, 0);
    const int b_before = done[1];
    PRINT("priority queue, A drops below B: change A's priority", sceKernelChangeThreadPriority(threads[0], LOW_PRIORITY + 0x10));
    const int b_done = b_done_soon();
    PRINT("priority queue, before A drops: B is done", b_before);
    PRINT("priority queue, after A drops below B: B got its count", b_done);
    PRINT("priority queue, after A drops below B: A is done", done[0]);
    PRINT("priority queue, after A drops below B: the count", count());
    finish();
    PRINT("priority queue, A drops below B: A's result", result[0]);
    PRINT("priority queue, A drops below B: B's result", result[1]);
}

static void callback_case(SceUInt32 attr, const char *name) {
    start_waiters(attr, LOW_PRIORITY, LOW_PRIORITY, TEST_WAIT_US, 1);
    PRINTF(sceKernelNotifyCallback(callback, 1), "%s, A runs a callback: notify it", name);
    TEST_WAIT_FOR(in_callback);
    // The callback takes 300 ms, so whatever reaches B meanwhile does so while A is in it
    const int b_done = b_done_soon() && !callback_done;
    TEST_WAIT_FOR(callback_done);
    // Give A time to queue again
    test_wait_until_waiting(threads[0]);
    PRINTF(in_callback, "%s, A runs a callback: it ran", name);
    PRINTF(b_done, "%s, while A runs a callback: B got its count", name);
    PRINTF(done[1], "%s, after the callback: B is done", name);
    PRINTF(done[0], "%s, after the callback: A is done", name);
    PRINTF(count(), "%s, after the callback: the count", name);
    finish();
    PRINTF(result[0], "%s, A runs a callback: A's result", name);
    PRINTF(result[1], "%s, A runs a callback: B's result", name);
}

int main(void) {
    signal_case(0, "FIFO queue");
    signal_case(SCE_KERNEL_ATTR_THREAD_PRIO, "priority queue");
    timeout_case(0, "FIFO queue, A times out");
    timeout_case(SCE_KERNEL_ATTR_THREAD_PRIO, "priority queue, A times out");
    priority_case();
    callback_case(0, "FIFO queue");
    callback_case(SCE_KERNEL_ATTR_THREAD_PRIO, "priority queue");

    test_exit();
    return 0;
}
