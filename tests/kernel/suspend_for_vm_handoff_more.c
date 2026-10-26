// Whether a waiter that sceKernelSuspendThreadForVM holds gets the object at the wake, or only once
// it is resumed, for the kinds suspend_for_vm_handoff doesn't have: a semaphore signal, a timer
// event, a multiple event wait that a set ends, the end of a thread, and a delay that ends at its
// time. W waits on core 0 with the VM attribute, and the main thread, on core 1, suspends it, lets
// the wake happen, reads what the object holds and tries to take it itself, then resumes W. Each
// case prints the suspend's result, the wake's, W's status and wait type once it no longer waits,
// runs or is READY (or about 3 s after the wake), what the object holds and the main thread's take,
// then W's result after the resume and what it got. The timer is started once W is suspended, so
// its event comes 200 ms later. The multiple event wait lists a second simple event that is never
// set. The thread end target waits for a gate and returns a status, which W's wait gets. The delay
// lasts 1 s, and the main thread waits for W to settle, which takes that long.
//
// The model suspend_for_vm_handoff covers the mutex, lightweight mutex, event flag, reader/writer
// lock, message pipe, condition, lightweight condition, simple event and thread signal kinds, and
// thread_status_matrix shows a semaphore signal takes the count for such a waiter.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// How long the delay case delays, and how long the timer takes after it is started
#define DELAY_US 1000000
#define TIMER_US 200000
// The exit status the target of the thread end case returns
#define TARGET_STATUS 0x3C
// How long the main thread waits for W to settle after the wake, in 1 ms steps
#define SETTLE_STEPS 3000

typedef enum Kind { SEMA, TIMER_EVENT, MULTI_EVENTS, THREAD_END, DELAY_END } Kind;

static Kind kind;
static SceUID object, second, gate, target;
static WaitEvent events[2];
static ResultEvent entries[2];
static volatile int result;
static int exit_status;

// Returns the status of the target when its gate is signaled
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return TARGET_STATUS;
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 3 * TEST_WAIT_US;
    SceUInt32 bits;
    switch (kind) {
    case SEMA: result = sceKernelWaitSema(object, 1, &timeout); break;
    case TIMER_EVENT: result = sceKernelWaitEvent(object, EVENT_TIMER, &bits, NULL, &timeout); break;
    case MULTI_EVENTS:
        result = sceKernelWaitMultipleEvents(events, 2, EVENT_WAIT_MODE_OR, entries, &timeout);
        break;
    case THREAD_END: result = sceKernelWaitThreadEnd(target, &exit_status, &timeout); break;
    case DELAY_END: result = sceKernelDelayThread(DELAY_US); break;
    }
    return 0;
}

static SceUInt32 wait_type_of(Kind k) {
    switch (k) {
    case SEMA: return WAIT_TYPE_SEMA;
    case TIMER_EVENT: return WAIT_TYPE_EVENT;
    case MULTI_EVENTS: return WAIT_TYPE_MP_EVENTS;
    case THREAD_END: return WAIT_TYPE_THREAD_END;
    case DELAY_END: return WAIT_TYPE_DELAY;
    }
    return 0;
}

// Whether thread is in a wait of the given type
static int waits_in(SceUID thread, SceUInt32 type) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType == type;
}

// Returns whether thread has ended and is dormant, so it can be deleted
static int is_dormant(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.status == SCE_THREAD_DORMANT;
}

static void create_object(Kind k) {
    switch (k) {
    case SEMA: object = sceKernelCreateSema("sema", 0, 0, 1, NULL); break;
    case TIMER_EVENT: {
        SceUInt64 interval = TIMER_US;
        object = sceKernelCreateTimer("timer", EVENT_ATTR_AUTO_RESET, NULL);
        sceKernelSetTimerEvent(object, TIMER_TYPE_SET_EVENT, &interval, 0);
        break;
    }
    case MULTI_EVENTS:
        object = sceKernelCreateSimpleEvent("event a", EVENT_ATTR_AUTO_RESET, 0, NULL);
        second = sceKernelCreateSimpleEvent("event b", EVENT_ATTR_AUTO_RESET, 0, NULL);
        events[0] = (WaitEvent){ object, EVENT_BIT_A };
        events[1] = (WaitEvent){ second, EVENT_BIT_B };
        break;
    case THREAD_END:
        gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
        target = test_start_thread(target_entry);
        break;
    case DELAY_END: break;
    }
}

// Makes the wait of W end, and returns the result of the call that did it
static int wake(Kind k) {
    switch (k) {
    case SEMA: return sceKernelSignalSema(object, 1);
    case TIMER_EVENT: return sceKernelStartTimer(object);
    case MULTI_EVENTS: return sceKernelSetEvent(object, EVENT_BIT_A, 0x11);
    case THREAD_END: return sceKernelSignalSema(gate, 1);
    case DELAY_END: return 0;
    }
    return 0;
}

// Prints what the object holds while W is suspended, and what a take by the main thread gets.
// Whatever the main thread takes it gives back, so W can still get it after the resume.
static void print_object(const char *name, Kind k) {
    switch (k) {
    case SEMA: {
        SceKernelSemaInfo info = { .size = sizeof(info) };
        sceKernelGetSemaInfo(object, &info);
        PRINTF(info.currentCount, "%s: the count", name);
        PRINTF(info.numWaitThreads, "%s: the waiters", name);
        const int try_take = sceKernelPollSema(object, 1);
        PRINTF(try_take, "%s: poll the semaphore", name);
        if (try_take == 0)
            sceKernelSignalSema(object, 1);
        break;
    }
    case TIMER_EVENT: {
        EventInfo info = { .size = sizeof(info) };
        sceKernelGetEventInfo(object, &info);
        PRINTF(info.eventPattern & EVENT_TIMER, "%s: the timer bit after the fire", name);
        PRINTF(info.numWaitThreads, "%s: the waiters", name);
        break;
    }
    case MULTI_EVENTS: {
        EventInfo info = { .size = sizeof(info) };
        sceKernelGetEventInfo(object, &info);
        PRINTF(info.eventPattern & EVENT_BIT_A, "%s: the bit after the set", name);
        PRINTF(info.numWaitThreads, "%s: the waiters", name);
        break;
    }
    case THREAD_END:
        TEST_WAIT_FOR(is_dormant(target));
        PRINTF(is_dormant(target), "%s: the target is dormant", name);
        break;
    case DELAY_END: break;
    }
}

static void delete_object(Kind k) {
    switch (k) {
    case SEMA: sceKernelDeleteSema(object); break;
    case TIMER_EVENT:
        sceKernelStopTimer(object);
        sceKernelDeleteTimer(object);
        break;
    case MULTI_EVENTS:
        sceKernelDeleteSimpleEvent(object);
        sceKernelDeleteSimpleEvent(second);
        break;
    case THREAD_END:
        sceKernelDeleteThread(target);
        sceKernelDeleteSema(gate);
        break;
    case DELAY_END: break;
    }
}

static void run_case(const char *name, Kind k) {
    kind = k;
    result = 0xDEAD;
    exit_status = 0xDEAD;
    create_object(k);
    const SceUID w = sceKernelCreateThread(
        "w", waiter, 0xA0, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(waits_in(w, wait_type_of(k)));
    const int suspended = sceKernelSuspendThreadForVM(w);
    const int woken = wake(k);
    // Waits for W to stop waiting and running. On a Vita the wake changes W's info before it
    // returns, but a waiter woken by a signal can show READY until its core takes it and finds it
    // suspended. A timer or a delay ends later than the wake call, so the loop gives it the time.
    SceKernelThreadInfo info = { .size = sizeof(info) };
    for (int i = 0; i < SETTLE_STEPS; i++) {
        sceKernelGetThreadInfo(w, &info);
        if (info.waitType == 0 && !(info.status & (SCE_THREAD_RUNNING | SCE_THREAD_READY)))
            break;
        sceKernelDelayThread(1000);
    }
    PRINTF(suspended, "%s: suspend W", name);
    PRINTF(woken, "%s: the wake", name);
    PRINTF(info.status, "%s: W's status after it", name);
    PRINTF(info.waitType, "%s: W's wait type after it", name);
    print_object(name, k);
    PRINTF(sceKernelResumeThreadForVM(w), "%s: resume W", name);
    test_join_thread(w);
    PRINTF(result, "%s: W's result", name);
    if (k == MULTI_EVENTS && result > 0) {
        PRINTF(entries[0].eventId == object ? 1 : entries[0].eventId == second ? 2 : 0,
            "%s: W's first entry is for event a or b", name);
        PRINTF(entries[0].result, "%s: W's first entry", name);
        PRINTF((int)entries[0].userData, "%s: W's first entry's user data", name);
    }
    if (k == THREAD_END)
        PRINTF(exit_status, "%s: the exit status W got", name);
    delete_object(k);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("semaphore signal", SEMA);
    run_case("timer event", TIMER_EVENT);
    run_case("multiple events set", MULTI_EVENTS);
    run_case("thread end", THREAD_END);
    run_case("delay end", DELAY_END);

    test_exit();
    return 0;
}
