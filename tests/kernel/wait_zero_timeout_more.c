// Blocking waits with a timeout of 0 for the kinds that the model wait_zero_timeout leaves out: a
// timer whose event isn't due, a lightweight condition variable, a thread signal with none pending,
// and a delay of 0 microseconds, each with and without callbacks. Each wait prints its result and,
// where it has a timeout, the time it wrote back. The model covers the other kinds, each when it
// can end at once and when it can't. The delay takes no timeout, so only its result is printed, and
// a delay of 0 is refused with an invalid argument, as thread_edges shows.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// Far away, so the timer's event is not due during the test
#define NOT_DUE_US 10000000

enum Kind { TIMER, TIMER_CB, LW_COND, LW_COND_CB, SIGNAL, SIGNAL_CB, DELAY, DELAY_CB, KIND_COUNT };

static const char *const kind_names[] = {
    "timer, event not due",           "timer with callbacks, event not due",
    "lightweight condition variable", "lightweight condition variable with callbacks",
    "thread signal, none pending",    "thread signal with callbacks, none pending",
    "delay of 0 microseconds",        "delay of 0 microseconds with callbacks",
};

// Runs one wait of the kind with a timeout of 0 and returns its result. The
// delays take no timeout.
static int do_wait(int kind, SceUInt *timeout) {
    int result = 0;
    static SceKernelLwMutexWork lw_mutex;
    static SceKernelLwCondWork lw_cond;
    switch (kind) {
    case TIMER:
    case TIMER_CB: {
        const SceUID timer = sceKernelCreateTimer("timer", 0, NULL);
        SceUInt64 interval = NOT_DUE_US;
        sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
        sceKernelStartTimer(timer);
        SceUInt32 bits;
        SceUInt64 user_data;
        if (kind == TIMER)
            result = sceKernelWaitEvent(timer, EVENT_TIMER, &bits, &user_data, timeout);
        else
            result = sceKernelWaitEventCB(timer, EVENT_TIMER, &bits, &user_data, timeout);
        sceKernelStopTimer(timer);
        sceKernelDeleteTimer(timer);
        break;
    }
    case LW_COND:
    case LW_COND_CB:
        sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "lw cond", 0, &lw_mutex, NULL);
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        if (kind == LW_COND)
            result = sceKernelWaitLwCond(&lw_cond, timeout);
        else
            result = sceKernelWaitLwCondCB(&lw_cond, timeout);
        sceKernelUnlockLwMutex(&lw_mutex, 1);
        sceKernelDeleteLwCond(&lw_cond);
        sceKernelDeleteLwMutex(&lw_mutex);
        break;
    case SIGNAL: result = sceKernelWaitSignal(0, 0, timeout); break;
    case SIGNAL_CB: result = sceKernelWaitSignalCB(0, 0, timeout); break;
    case DELAY: result = sceKernelDelayThread(0); break;
    case DELAY_CB: result = sceKernelDelayThreadCB(0); break;
    }
    return result;
}

int main(void) {
    for (int kind = 0; kind < KIND_COUNT; kind++) {
        SceUInt timeout = 0xDEAD;
        PRINTF(do_wait(kind, &timeout), "%s: the wait", kind_names[kind]);
        if (kind < DELAY)
            PRINTF(timeout, "%s: the time it wrote back", kind_names[kind]);
    }

    test_exit();
    return 0;
}
