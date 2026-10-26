// The order an auto reset timer wakes three waiters in, for the attributes and priorities that
// queue_order_matrix uses for its other kinds. The waiters queue in the order 0, 1, 2, each once
// the one before waits, either at the priorities 0x90, 0x70, 0x80 or all at 0x80, and once more at
// 0x90, 0x70, 0x80 with waiter 2 raised to 0x60 while all three wait. The timer queues its waiters
// in arrival order with attribute 0, and by priority with attribute 0x2000. Each case starts a new
// timer, and each of its three events is set 100 ms ahead of its start, so one event lets one
// waiter through, after the waiter before it has been woken. The other kinds are in
// queue_order_matrix and queue_order_matrix_more. This test has only the timer cases, so a stall on
// the timer events can't take the other rows down. The order prints as one hex digit per waiter,
// first woken first, so 0x012 is FIFO order and 0x120 is priority order for the first set of
// priorities.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// The delay of each timer event, long enough that the waiter of the event is waiting by then
#define TIMER_INTERVAL_US 100000

static SceUID timer;
static volatile int done_count;
static int order[3];
static volatile int result[3];

static void woke(int i) {
    const int n = __atomic_fetch_add(&done_count, 1, __ATOMIC_SEQ_CST);
    if (n < 3)
        order[n] = i;
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    SceUInt32 bits;
    if ((result[i] = sceKernelWaitEvent(timer, EVENT_TIMER, &bits, NULL, &timeout)) == 0)
        woke(i);
    return 0;
}

// Sets the timer's event to fire once, TIMER_INTERVAL_US from now, and starts the timer
static void fire(void) {
    SceUInt64 interval = TIMER_INTERVAL_US;
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    sceKernelStartTimer(timer);
}

static void run(const char *name, SceUInt32 attr, const int *priorities, const char *priority_name,
                int raise_last) {
    done_count = 0;
    for (int i = 0; i < 3; i++) {
        order[i] = 0xF;
        result[i] = 0xDEAD;
    }
    timer = sceKernelCreateTimer("timer", EVENT_ATTR_AUTO_RESET | attr, NULL);
    SceUID threads[3];
    for (int i = 0; i < 3; i++) {
        threads[i] = sceKernelCreateThread("waiter", waiter, priorities[i], 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(threads[i], sizeof(i), &i);
        test_wait_until_waiting(threads[i]);
    }
    if (raise_last)
        sceKernelChangeThreadPriority(threads[2], 0x60);
    for (int n = 0; n < 3; n++) {
        fire();
        TEST_WAIT_FOR(done_count > n);
    }
    for (int i = 0; i < 3; i++)
        test_join_thread(threads[i]);
    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
    const int value = (order[0] << 8) | (order[1] << 4) | order[2];
    PRINTF(value, "%s, attr 0x%X, priorities %s: wake order", name, (unsigned)attr, priority_name);
    if (done_count != 3)
        PRINTF(done_count, "%s, attr 0x%X, priorities %s: waiters woken", name, (unsigned)attr,
               priority_name);
}

int main(void) {
    static const int mixed[3] = { 0x90, 0x70, 0x80 };
    static const int same[3] = { 0x80, 0x80, 0x80 };
    static const SceUInt32 attrs[] = { 0, SCE_KERNEL_ATTR_THREAD_PRIO };

    for (unsigned a = 0; a < sizeof(attrs) / sizeof(attrs[0]); a++) {
        run("timer, auto reset", attrs[a], mixed, "0x90 0x70 0x80", 0);
        run("timer, auto reset", attrs[a], same, "all 0x80", 0);
        run("timer, auto reset", attrs[a], mixed, "0x90 0x70 0x80, then waiter 2 raised to 0x60",
            1);
    }

    test_exit();
    return 0;
}
