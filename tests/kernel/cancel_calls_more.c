// What sceKernelCancelEvent and sceKernelCancelTimer do to a simple event or a timer with one
// waiter, with two waiters, and with a multiple event wait that lists it. Each case prints the
// cancel's result and the waiting count it writes, what each waiter gets back, and what the object
// holds after the waiters ended: the waiting threads and the pattern of a simple event, or the
// waiting threads and whether a timer is still active. The multiple event wait is an OR wait on the
// object and on a second simple event that is never set, so its entries are printed with the event
// each is for (1 for the object, 2 for the other event). Each case has a fresh object. A timer is
// started with an interval of 30 s, so it never fires during the test.
//
// The model covers the cancel calls of semaphores, event flags, mutexes, reader/writer locks and
// message pipes in cancel_calls, two waiters on a simple event in simple_event_cancel, and two
// waiters on a timer in timer_info_cancel. A single waiter with a count pointer and the multiple
// event wait are new here.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// 30 s, far past the end of the test
#define INTERVAL 30000000

enum Kind { SIMPLE_EVENT, TIMER };
enum Way { ONE_WAITER, TWO_WAITERS, MULTIPLE_WAIT };

static const char *const way_names[] = { "1 waiter", "2 waiters", "a multiple event wait" };

static enum Kind kind;
static SceUID object, other;
static SceUInt32 object_bit;
static volatile int results[2];
static WaitEvent list[2];
static ResultEvent entries[2];
static volatile int multi_result;

// Waits for the object's bit, storing the result in the slot given by argp
static int single_waiter(SceSize args, void *argp) {
    (void)args;
    const int slot = *(const int *)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    SceUInt32 pattern;
    SceUInt64 user_data;
    results[slot] = sceKernelWaitEvent(object, object_bit, &pattern, &user_data, &timeout);
    return 0;
}

// Waits for either event of list, in OR mode
static int multi_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    multi_result = sceKernelWaitMultipleEvents(list, 2, EVENT_WAIT_MODE_OR, entries, &timeout);
    return 0;
}

// Creates the object of the kind, which is set to wait for its bit, and the other event
static void create_object(void) {
    if (kind == SIMPLE_EVENT) {
        object = sceKernelCreateSimpleEvent("event", EVENT_ATTR_MANUAL_RESET, 0, NULL);
        object_bit = EVENT_BIT_A;
    } else {
        SceUInt64 interval = INTERVAL;
        object = sceKernelCreateTimer("timer", EVENT_ATTR_AUTO_RESET, NULL);
        sceKernelSetTimerEvent(object, TIMER_TYPE_SET_EVENT, &interval, 0);
        sceKernelStartTimer(object);
        object_bit = EVENT_TIMER;
    }
    other = sceKernelCreateSimpleEvent("other", EVENT_ATTR_MANUAL_RESET, 0, NULL);
}

static void delete_object(void) {
    if (kind == SIMPLE_EVENT) {
        sceKernelDeleteSimpleEvent(object);
    } else {
        sceKernelStopTimer(object);
        sceKernelDeleteTimer(object);
    }
    sceKernelDeleteSimpleEvent(other);
}

static int cancel(SceUInt32 *waiting) {
    if (kind == SIMPLE_EVENT)
        return sceKernelCancelEvent(object, waiting);
    return sceKernelCancelTimer(object, waiting);
}

// Prints what the object holds after the waiters ended
static void print_after(const char *what) {
    if (kind == SIMPLE_EVENT) {
        EventInfo info = { .size = sizeof(info) };
        sceKernelGetEventInfo(object, &info);
        PRINTF(info.numWaitThreads, "%s: the waiting threads after", what);
        PRINTF(info.eventPattern, "%s: the pattern after", what);
    } else {
        TimerInfo info = { .size = sizeof(info) };
        sceKernelGetTimerInfo(object, &info);
        PRINTF(info.numWaitThreads, "%s: the waiting threads after", what);
        PRINTF(info.active, "%s: it is active after", what);
    }
}

// Runs one cancel with the waiters of way, then prints what the cancel and the waiters report
static void run_case(const char *what, enum Way way) {
    SceUID threads[2];
    const int count = way == TWO_WAITERS ? 2 : 1;
    SceUInt32 waiting = 0xDEAD;
    create_object();
    if (way == MULTIPLE_WAIT) {
        list[0] = (WaitEvent){ object, object_bit };
        list[1] = (WaitEvent){ other, EVENT_BIT_B };
        multi_result = 0xDEAD;
        sceClibMemset(entries, 0xAA, sizeof(entries));
        threads[0] = test_start_thread(multi_waiter);
        test_wait_until_waiting(threads[0]);
    } else {
        for (int i = 0; i < count; i++) {
            results[i] = -1;
            threads[i] = test_start_thread_arg(single_waiter, i);
            test_wait_until_waiting(threads[i]);
        }
    }

    PRINTF(cancel(&waiting), "%s: cancel with %s", what, way_names[way]);
    PRINTF(waiting, "%s: the waiting count", what);
    if (way == MULTIPLE_WAIT) {
        test_join_thread(threads[0]);
        PRINTF(multi_result, "%s: the multiple event wait", what);
        for (int i = 0; i < 2 && i < multi_result; i++) {
            PRINTF(entries[i].eventId == object ? 1 : entries[i].eventId == other ? 2 : 0,
                "%s: entry %d is for", what, i);
            PRINTF(entries[i].result, "%s: entry %d", what, i);
        }
    } else {
        for (int i = 0; i < count; i++) {
            test_join_thread(threads[i]);
            PRINTF(results[i], "%s: waiter %d", what, i);
        }
    }
    print_after(what);
    delete_object();
}

int main(void) {
    kind = SIMPLE_EVENT;
    run_case("simple event", ONE_WAITER);
    run_case("simple event", TWO_WAITERS);
    run_case("simple event", MULTIPLE_WAIT);

    kind = TIMER;
    run_case("timer", ONE_WAITER);
    run_case("timer", TWO_WAITERS);
    run_case("timer", MULTIPLE_WAIT);

    test_exit();
    return 0;
}
