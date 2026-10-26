// What a call that wakes several waiters on one event object leaves behind for the first of them to
// run. Two threads wait on the object: the early waiter at 0x50, which starts first, and the late
// waiter at 0xB0. Both are pinned to core 0 and the main thread runs at 0x60 between them, so the
// early waiter preempts the main thread when it starts, and the late one stays ready until the
// main thread blocks. Each case starts the early waiter and waits until it waits, then starts the
// late one and waits until that waits too. The main thread records 0 before the call and 2 after
// it. The first waiter to run after the call, called the reader here, records 1 and then reads the
// object's number of waiting threads and its bits, and the other waiter's status, before it does
// anything else. The other waiter, once its wait ends, holds on a semaphore until the main thread
// has joined the reader, so it can't end before the reader has read its status. After the call the
// main thread joins the reader, posts that semaphore, joins the other waiter, and prints everything
// after the joins. The cases:
// - an event flag with SCE_EVENT_WAITMULTIPLE and a FIFO queue. The early waiter waits for 0x1
//   with OR, the late one for 0x1 with OR and SCE_EVENT_WAITCLEAR. Then a set of 0x1. The reader
//   is the early waiter.
// - the same with the priority queue attribute, and the late waiter raised to 0x4E while it waits,
//   so it is first in the queue. Its clear leaves no bits for the early waiter, which stays queued,
//   so the reader is the late waiter. After it has run, a second set of 0x1 wakes the early waiter.
// - an auto reset simple event. The early waiter waits for A, the late one for B. Then a set of A
//   and B.
// - a manual reset simple event with the same waiters. Then a pulse of A and B.
// Every wait has a timeout of three TEST_WAIT_US, so a call that doesn't wake a waiter can't hang.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define EARLY_PRIORITY 0x50
#define LATE_PRIORITY 0xB0
#define RAISED_PRIORITY 0x4E
#define NOT_DONE 0xDEAD
#define LONG_TIMEOUT (3 * TEST_WAIT_US)
#define MAX_STEPS 3

enum { EARLY, LATE };

typedef enum Kind { FLAG, SIMPLE } Kind;

static Kind kind;
static SceUID object, gate;
static SceUID threads[2];
static int reader;
static SceUInt32 wanted[2], mode[2];
static volatile int result[2];
static volatile SceUInt32 bits[2];
static int step_count, steps[MAX_STEPS];
static int seen_waiting, seen_other_status;
static SceUInt32 seen_bits;

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < MAX_STEPS)
        steps[i] = step;
}

// Returns the status of thread, or NOT_DONE if the call fails
static int thread_status(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? (int)info.status : NOT_DONE;
}

// Reads the number of waiting threads of the object and its bits
static void read_object(int *waiting, SceUInt32 *now) {
    if (kind == FLAG) {
        SceKernelEventFlagInfo info = { .size = sizeof(info) };
        sceKernelGetEventFlagInfo(object, &info);
        *waiting = info.numWaitThreads;
        *now = info.currentPattern;
    } else {
        EventInfo info = { .size = sizeof(info) };
        sceKernelGetEventInfo(object, &info);
        *waiting = info.numWaitThreads;
        *now = info.eventPattern;
    }
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = LONG_TIMEOUT;
    SceUInt32 out = NOT_DONE;
    if (kind == FLAG)
        result[i] = sceKernelWaitEventFlag(object, wanted[i], mode[i], &out, &timeout);
    else
        result[i] = sceKernelWaitEvent(object, wanted[i], &out, NULL, &timeout);
    bits[i] = out;
    if (i == reader) {
        record(1);
        read_object(&seen_waiting, &seen_bits);
        seen_other_status = thread_status(threads[1 - i]);
    } else {
        SceUInt gate_timeout = LONG_TIMEOUT;
        sceKernelWaitSema(gate, 1, &gate_timeout);
    }
    return 0;
}

// Starts the waiter with index i at priority, pinned to core 0, and waits until it waits
static SceUID start_waiter(int i, int priority) {
    const SceUID thread = sceKernelCreateThread("waiter", waiter, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, sizeof(i), &i);
    test_wait_until_waiting(thread);
    return thread;
}

// The call that wakes the waiters: a set of bits, or for a simple event a set or a pulse of them
static int wake(SceUInt32 call_bits, int pulse) {
    if (kind == FLAG)
        return sceKernelSetEventFlag(object, call_bits);
    if (pulse)
        return sceKernelPulseEvent(object, call_bits, 0);
    return sceKernelSetEvent(object, call_bits, 0);
}

// Runs one case on the object already created. With raise, the late waiter is raised before the
// call, and the early waiter is woken by a second set once the late waiter has run.
static void run(const char *name, SceUInt32 call_bits, int pulse, int raise) {
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = -1;
    for (int i = 0; i < 2; i++) {
        result[i] = NOT_DONE;
        bits[i] = NOT_DONE;
    }
    seen_waiting = NOT_DONE;
    seen_bits = NOT_DONE;
    seen_other_status = NOT_DONE;
    reader = raise ? LATE : EARLY;
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);

    threads[EARLY] = start_waiter(EARLY, EARLY_PRIORITY);
    threads[LATE] = start_waiter(LATE, LATE_PRIORITY);
    if (raise)
        sceKernelChangeThreadPriority(threads[LATE], RAISED_PRIORITY);

    record(0);
    const int call = wake(call_bits, pulse);
    record(2);

    test_join_thread(threads[reader]);
    int second = 0;
    if (raise)
        second = sceKernelSetEventFlag(object, 0x1);
    sceKernelSignalSema(gate, 1);
    test_join_thread(threads[1 - reader]);
    sceKernelDeleteSema(gate);

    int waiting_after;
    SceUInt32 bits_after;
    read_object(&waiting_after, &bits_after);

    PRINTF(call, "%s: the call", name);
    const int count = step_count < MAX_STEPS ? step_count : MAX_STEPS;
    for (int i = 0; i < count; i++)
        PRINTF(steps[i], "%s: step %d", name, i);
    PRINTF(seen_waiting, "%s: the waiting threads when the first waiter runs", name);
    PRINTF(seen_bits, "%s: the bits when the first waiter runs", name);
    PRINTF(seen_other_status, "%s: the other waiter's status when the first waiter runs", name);
    PRINTF(result[reader], "%s: the first waiter's result", name);
    PRINTF(bits[reader], "%s: the first waiter's bits", name);
    if (raise)
        PRINTF(second, "%s: the second set", name);
    PRINTF(result[1 - reader], "%s: the other waiter's result", name);
    PRINTF(bits[1 - reader], "%s: the other waiter's bits", name);
    PRINTF(waiting_after, "%s: the waiting threads after the joins", name);
    PRINTF(bits_after, "%s: the bits after the joins", name);
}

static void flag_case(const char *name, SceUInt32 queue, int raise) {
    kind = FLAG;
    object = sceKernelCreateEventFlag("flag", queue | SCE_EVENT_WAITMULTIPLE, 0, NULL);
    wanted[EARLY] = wanted[LATE] = 0x1;
    mode[EARLY] = SCE_EVENT_WAITOR;
    mode[LATE] = SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR;
    run(name, 0x1, 0, raise);
    sceKernelDeleteEventFlag(object);
}

static void event_case(const char *name, SceUInt32 attr, int pulse) {
    kind = SIMPLE;
    object = sceKernelCreateSimpleEvent("event", attr, 0, NULL);
    wanted[EARLY] = EVENT_BIT_A;
    wanted[LATE] = EVENT_BIT_B;
    run(name, EVENT_BIT_A | EVENT_BIT_B, pulse, 0);
    sceKernelDeleteSimpleEvent(object);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    flag_case("event flag, FIFO", 0, 0);
    flag_case("event flag, priority queue, late waiter raised", SCE_KERNEL_ATTR_THREAD_PRIO, 1);
    event_case("auto reset simple event, set", EVENT_ATTR_AUTO_RESET, 0);
    event_case("manual reset simple event, pulse", EVENT_ATTR_MANUAL_RESET, 1);

    test_exit();
    return 0;
}
