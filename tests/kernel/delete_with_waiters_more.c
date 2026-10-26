// Deleting what 2 threads wait on, for the kinds delete_with_waiters doesn't have: a multiple event
// wait with OR and with AND, where the listed simple event or timer is deleted, and a thread end
// wait on a thread that is deleted right after it ends. Each case has a plain waiter and a waiter
// with callbacks, and prints the result of the delete and each waiter's result and time left. A
// multiple event wait also lists a second simple event that is never set, so an OR wait ends with
// the first delete, while an AND wait goes on until the second event is deleted too. The AND waits
// are checked to still wait between the two deletes. The timer is never started, so it never fires.
// For a multiple event wait, each entry it filled is printed with the event it is for (1 for the
// first listed event, 2 for the second). The thread end case prints the exit status each waiter
// got.
//
// The model covers the deletes of the other sync objects, and of a timer that is waited on
// directly, in delete_with_waiters. An AND wait whose simple events are deleted is in
// multi_events_and_delete.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// The exit status the target of the thread end case returns
#define TARGET_STATUS 0x3C

enum Kind { OR_SIMPLE, AND_SIMPLE, OR_TIMER, AND_TIMER, THREAD_END, KIND_COUNT };
static const char *const kind_names[] = {
    "multiple events OR, listed simple event deleted",
    "multiple events AND, listed simple event deleted",
    "multiple events OR, listed timer deleted",
    "multiple events AND, listed timer deleted",
    "thread end, thread deleted after it ends",
};
static const char *const waiter_names[] = { "the plain waiter", "the waiter with callbacks" };

static int kind;
static SceUID first, second, gate, target;
static SceUInt32 first_bit;
static int second_deleted;
static WaitEvent events[2];
static volatile int wait_result[2];
static SceUInt left[2];
static ResultEvent entries[2][2];
static int exit_status[2];

// Waits for the target's end, or for both listed events, with callbacks in slot 1
static int waiter(SceSize args, void *argp) {
    (void)args;
    const int slot = *(const int *)argp;
    const int cb = slot == 1;
    SceUInt timeout = TEST_WAIT_US;
    switch (kind) {
    case THREAD_END:
        wait_result[slot] = cb ? sceKernelWaitThreadEndCB(target, &exit_status[slot], &timeout)
                               : sceKernelWaitThreadEnd(target, &exit_status[slot], &timeout);
        break;
    case OR_SIMPLE:
    case OR_TIMER:
    case AND_SIMPLE:
    case AND_TIMER: {
        const SceUInt32 mode = kind == OR_SIMPLE || kind == OR_TIMER ? EVENT_WAIT_MODE_OR : EVENT_WAIT_MODE_AND;
        wait_result[slot] = cb ? sceKernelWaitMultipleEventsCB(events, 2, mode, entries[slot], &timeout)
                               : sceKernelWaitMultipleEvents(events, 2, mode, entries[slot], &timeout);
        break;
    }
    case KIND_COUNT: break;
    }
    left[slot] = timeout;
    return 0;
}

// Waits for the end of the target, which returns TARGET_STATUS once the gate is signaled
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return TARGET_STATUS;
}

// Creates the listed simple event or timer, which is not started, and the second simple event
static void create_listed(void) {
    if (kind == OR_SIMPLE || kind == AND_SIMPLE) {
        first = sceKernelCreateSimpleEvent("first", EVENT_ATTR_AUTO_RESET, 0, NULL);
        first_bit = EVENT_BIT_A;
    } else {
        first = sceKernelCreateTimer("timer", 0, NULL);
        first_bit = EVENT_TIMER;
    }
    second = sceKernelCreateSimpleEvent("second", EVENT_ATTR_AUTO_RESET, 0, NULL);
    events[0] = (WaitEvent){ first, first_bit };
    events[1] = (WaitEvent){ second, EVENT_BIT_B };
}

static int delete_first(void) {
    if (kind == OR_SIMPLE || kind == AND_SIMPLE)
        return sceKernelDeleteSimpleEvent(first);
    return sceKernelDeleteTimer(first);
}

static int delete_second(void) {
    second_deleted = 1;
    return sceKernelDeleteSimpleEvent(second);
}

// Returns whether thread is in a wait for multiple events, with or without callbacks
static int in_multiple_wait(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && (info.waitType & ~WAIT_TYPE_CB) == WAIT_TYPE_MP_EVENTS;
}

// Returns whether thread has ended and is dormant, so it can be deleted
static int is_dormant(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.status == SCE_THREAD_DORMANT;
}

// 1 for the first listed event, 2 for the second, 0 for another UID
static int which_event(SceUID id) {
    return id == first ? 1 : id == second ? 2 : 0;
}

// 0 if no time is left, 1 if some is, 2 if all of it is
static int time_left(SceUInt value) {
    return value == 0 ? 0 : value >= TEST_WAIT_US ? 2 : 1;
}

int main(void) {
    for (kind = 0; kind < KIND_COUNT; kind++) {
        const char *name = kind_names[kind];
        SceUID threads[2];
        if (kind == THREAD_END) {
            gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
            target = test_start_thread(target_entry);
            test_wait_until_waiting(target);
        } else {
            create_listed();
        }
        second_deleted = 0;
        for (int i = 0; i < 2; i++) {
            wait_result[i] = -1;
            left[i] = 0xDEAD;
            exit_status[i] = 0xDEAD;
            sceClibMemset(entries[i], 0xAA, sizeof(entries[i]));
            threads[i] = test_start_thread_arg(waiter, i);
            test_wait_until_waiting(threads[i]);
        }

        if (kind == THREAD_END) {
            sceKernelSignalSema(gate, 1);
            TEST_WAIT_FOR(is_dormant(target));
            PRINTF(sceKernelDeleteThread(target), "%s: delete with 2 waiters", name);
        } else {
            PRINTF(delete_first(), "%s: delete with 2 waiters", name);
            if (kind == AND_SIMPLE || kind == AND_TIMER) {
                for (int i = 0; i < 2; i++)
                    PRINTF(in_multiple_wait(threads[i]), "%s: %s still waits after it", name, waiter_names[i]);
                PRINTF(delete_second(), "%s: delete the second event", name);
            }
        }

        for (int i = 0; i < 2; i++) {
            test_join_thread(threads[i]);
            PRINTF(wait_result[i], "%s: %s", name, waiter_names[i]);
            PRINTF(time_left(left[i]), "%s: the time left of %s", name, waiter_names[i]);
            if (kind == THREAD_END) {
                PRINTF(exit_status[i], "%s: the exit status %s got", name, waiter_names[i]);
            } else if (wait_result[i] > 0) {
                for (int e = 0; e < 2 && e < wait_result[i]; e++) {
                    PRINTF(which_event(entries[i][e].eventId), "%s: entry %d of %s is for", name, e, waiter_names[i]);
                    PRINTF(entries[i][e].result, "%s: entry %d of %s", name, e, waiter_names[i]);
                }
            }
        }

        if (kind == THREAD_END) {
            sceKernelDeleteSema(gate);
        } else if (!second_deleted) {
            delete_second();
        }
    }

    test_exit();
    return 0;
}
