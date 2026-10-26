// A set or pulse with notification reaches the callback of a thread T that waits with callbacks on
// another object, not on the notified event. Each case has a fresh event E, a fresh object and a
// fresh T. T creates its callback, registers it to E, and waits with callbacks on the object with
// a timeout of 6 seconds. Once T waits, the main thread sends the notification to E, and reads
// what T and the object show right after it, with no call that can block in between. Then it
// blocks until the callback ran and T waits again, and reads the callback's arguments and the
// object again. Then it releases T and joins it, so the prints come after every thread has ended.
// Every thread is pinned to core 0. The main thread runs at 0x60 and T at 0xB0, so T only runs
// while the main thread blocks. The cases:
// - the object is a semaphore at 0, released by a signal
// - the object is an event flag, released by a set of bit 0x1
// - the object is a second simple event, released by a set of its bit A
// - the object is E itself, and T waits for bit B, which the notifying set of bit A doesn't give,
//   released by a set of bit B
// - T waits in DelayThreadCB, which has no object and is not released, so its wait ends at its
//   deadline
// - a pulse with notification, on a semaphore at 0
// Each case prints whether T's registration of its callback to E worked, then the notify call's
// result, the object's waiting threads, the callback's pending notifications, T's status and
// wait type, all right after the notify. Then the runs of the callback, its notify ID, count and
// argument, whether T waits again and the object's waiting threads then, the release, and T's
// wait result. For the delay it prints whether T's wait ended within 1 second of the notify.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define LONG_US (3 * TEST_WAIT_US)
// The delay of the delay case ends at its deadline, which is far past this
#define SOON_US 1000000

typedef enum Kind {
    SEMA,
    FLAG,
    SIMPLE,
    SELF,
    DELAY
} Kind;

static SceUID event, object;
static volatile SceUID callback;
static volatile int callback_runs, got_notify_id, got_count, got_arg;
static Kind current;
static volatile int wait_result, register_result;
static volatile SceUInt64 wait_end;

// Records the arguments of the notification, which the callback gets when T runs it
static int notified_callback(int notify_id, int count, int arg, void *common) {
    (void)common;
    callback_runs++;
    got_notify_id = notify_id;
    got_count = count;
    got_arg = arg;
    return 0;
}

// Creates its callback and registers it to E, then waits with callbacks for the current case
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("notified", 0, notified_callback, NULL);
    register_result = sceKernelRegisterCallbackToEvent(event, callback);
    SceUInt timeout = LONG_US;
    SceUInt32 bits = 0, pattern = 0;
    SceUInt64 user_data = 0;
    switch (current) {
    case SEMA: wait_result = sceKernelWaitSemaCB(object, 1, &timeout); break;
    case FLAG: wait_result = sceKernelWaitEventFlagCB(object, 0x1, SCE_EVENT_WAITOR, &bits, &timeout); break;
    case SIMPLE: wait_result = sceKernelWaitEventCB(object, EVENT_BIT_A, &pattern, &user_data, &timeout); break;
    case SELF: wait_result = sceKernelWaitEventCB(object, EVENT_BIT_B, &pattern, &user_data, &timeout); break;
    case DELAY: wait_result = sceKernelDelayThreadCB(LONG_US); break;
    }
    wait_end = sceKernelGetProcessTimeWide();
    sceKernelDeleteCallback(callback);
    return 0;
}

// Creates the object of the case. SELF waits on E itself.
static void create_object(Kind kind) {
    switch (kind) {
    case SEMA: object = sceKernelCreateSema("sema", 0, 0, 1, NULL); break;
    case FLAG: object = sceKernelCreateEventFlag("flag", 0, 0, NULL); break;
    case SIMPLE: object = sceKernelCreateSimpleEvent("second", EVENT_ATTR_MANUAL_RESET, 0, NULL); break;
    case SELF: object = event; break;
    case DELAY: object = 0; break;
    }
}

static void delete_object(Kind kind) {
    switch (kind) {
    case SEMA: sceKernelDeleteSema(object); break;
    case FLAG: sceKernelDeleteEventFlag(object); break;
    case SIMPLE: sceKernelDeleteSimpleEvent(object); break;
    case SELF:
    case DELAY: break;
    }
}

// Returns the wait type that T's wait on the object reports, without the callback bit
static SceUInt32 wait_type_for(Kind kind) {
    switch (kind) {
    case SEMA: return WAIT_TYPE_SEMA;
    case FLAG: return WAIT_TYPE_EVENT_FLAG;
    case SIMPLE:
    case SELF: return WAIT_TYPE_EVENT;
    case DELAY: return WAIT_TYPE_DELAY;
    }
    return 0;
}

// Returns the number of threads waiting on the object of the case, or NOT_DONE if it has none
static int waiting_threads(Kind kind) {
    switch (kind) {
    case SEMA: {
        SceKernelSemaInfo info = { .size = sizeof(info) };
        return sceKernelGetSemaInfo(object, &info) == 0 ? info.numWaitThreads : NOT_DONE;
    }
    case FLAG: {
        SceKernelEventFlagInfo info = { .size = sizeof(info) };
        return sceKernelGetEventFlagInfo(object, &info) == 0 ? info.numWaitThreads : NOT_DONE;
    }
    case SIMPLE:
    case SELF: {
        EventInfo info = { .size = sizeof(info) };
        return sceKernelGetEventInfo(object, &info) == 0 ? (int)info.numWaitThreads : NOT_DONE;
    }
    case DELAY: break;
    }
    return NOT_DONE;
}

// Returns the status of thread, which is 2 when it is ready and 8 when it waits
static int status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? (int)info.status : NOT_DONE;
}

// Returns the wait type of thread, where the callback bit marks a wait with callbacks
static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? info.waitType : NOT_DONE;
}

// Returns whether thread waits with callbacks on the object of the case, or in a delay with
// callbacks for the delay case
static int waits_with_callbacks(SceUID thread, Kind kind) {
    const SceUInt32 type = wait_type_for(kind) | WAIT_TYPE_CB;
    if (kind == DELAY)
        return wait_type_of(thread) == type;
    return test_in_wait(thread, type, object);
}

// Waits until the callback ran and T waits with callbacks again, for at most 2 seconds on the
// process clock. T's own deadline is 6 seconds after its wait began, so when the notification
// doesn't reach T, the release below still comes before that deadline.
static void wait_for_callback_again(SceUID thread, Kind kind) {
    const SceUInt64 end = sceKernelGetProcessTimeWide() + 2000000;
    while (!(callback_runs > 0 && waits_with_callbacks(thread, kind)) && sceKernelGetProcessTimeWide() < end)
        sceKernelDelayThread(1000);
}

// Ends the wait of the case, and returns the call's result. The delay has no release.
static int release(Kind kind) {
    switch (kind) {
    case SEMA: return sceKernelSignalSema(object, 1);
    case FLAG: return sceKernelSetEventFlag(object, 0x1);
    case SIMPLE: return sceKernelSetEvent(object, EVENT_BIT_A, 0);
    case SELF: return sceKernelSetEvent(object, EVENT_BIT_B, 0);
    case DELAY: break;
    }
    return NOT_DONE;
}

// Runs one case. A pulse with notification is sent instead of a set when pulse is 1.
static void run_case(Kind kind, const char *name, int pulse) {
    current = kind;
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_NOTIFY_CB_ALL, 0, NULL);
    create_object(kind);
    callback = 0;
    callback_runs = 0;
    got_notify_id = got_count = got_arg = NOT_DONE;
    wait_result = register_result = NOT_DONE;
    wait_end = 0;
    const SceUID thread = sceKernelCreateThread("t", waiter, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    TEST_WAIT_FOR(waits_with_callbacks(thread, kind));

    const int notify = pulse ? sceKernelPulseEventWithNotifyCallback(event, EVENT_BIT_A, 0, 0x66)
                             : sceKernelSetEventWithNotifyCallback(event, EVENT_BIT_A, 0, 0x55);
    const SceUInt64 notify_time = sceKernelGetProcessTimeWide();
    const int waiting = waiting_threads(kind);
    const int pending = sceKernelGetCallbackCount(callback);
    const int status = status_of(thread);
    const SceUInt32 wait_type = wait_type_of(thread);

    wait_for_callback_again(thread, kind);
    const int runs = callback_runs;
    const int id_is_event = got_notify_id == event;
    const int count = got_count;
    const int arg = got_arg;
    const int again = waits_with_callbacks(thread, kind);
    const int waiting_again = waiting_threads(kind);
    const int released = kind == DELAY ? NOT_DONE : release(kind);
    test_join_thread(thread);
    const int within = wait_end - notify_time < SOON_US;
    delete_object(kind);
    sceKernelDeleteSimpleEvent(event);

    PRINTF(register_result, "%s: T's registration of its callback to the event", name);
    PRINTF(notify, "%s: the notify", name);
    if (kind != DELAY)
        PRINTF(waiting, "%s: the waiting threads after the notify", name);
    PRINTF(pending, "%s: the callback's notifications after the notify", name);
    PRINTF(status, "%s: T's status after the notify", name);
    PRINTF(wait_type, "%s: T's wait type after the notify", name);
    PRINTF(runs, "%s: runs of the callback once T runs", name);
    PRINTF(id_is_event, "%s: the notify ID is the event", name);
    PRINTF(count, "%s: the count", name);
    PRINTF(arg, "%s: the argument", name);
    PRINTF(again, "%s: T waits again", name);
    if (kind != DELAY)
        PRINTF(waiting_again, "%s: the waiting threads after T waits again", name);
    if (kind != DELAY)
        PRINTF(released, "%s: the release", name);
    PRINTF(wait_result, "%s: T's wait result", name);
    if (kind == DELAY)
        PRINTF(within, "%s: T's wait ended within 1 s of the notify", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case(SEMA, "a semaphore", 0);
    run_case(FLAG, "an event flag", 0);
    run_case(SIMPLE, "a second simple event", 0);
    run_case(SELF, "the event itself", 0);
    run_case(DELAY, "a delay", 0);
    run_case(SEMA, "a pulse on a semaphore", 1);

    test_exit();
    return 0;
}
