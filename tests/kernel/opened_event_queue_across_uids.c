// Waiters on an event flag and a simple event through the UID that created it and through a UID
// that opened it, which share one queue. Each case creates the object with the openable attribute,
// opens it by its name, and starts one waiter through each UID. Then the main thread reads the
// waiting thread count through both UIDs, sets or polls through one of them, and reads again. It
// joins the waiters and prints their results, and the bits and user data they got. Every thread is
// pinned to core 0, the main thread at 0x60 and the waiters at 0xB0, so a woken waiter stays ready
// until the main thread blocks. The cases:
// - a multiple waiter event flag. W0 waits through the creating UID for bit 0x1 with OR and CLEAR,
//   and W1 through the opened UID for 0x1 with OR, which times out. The main thread checks the
//   count through both UIDs, sets 0x1 through the opened UID, then reads the count and pattern.
// - an event flag without the multiple waiter attribute. W0 waits through the creating UID for
//   bit 0x1. While it waits, the main thread makes a 1 ms wait and a poll through the opened UID,
//   and prints their results. A set through the opened UID then ends W0's wait, and a poll through
//   the opened UID afterwards is printed too.
// - an auto reset simple event. W0 waits through the creating UID for A and W1 through the opened
//   UID for A. A set of A through the opened UID goes to W0, and a second set of A through the
//   creating UID goes to W1.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
// The timeout of a wait that the case expects to end by its timeout, far past the checks before it
#define TIMEOUT_US TEST_WAIT_US
// The timeout of a wait that a set is expected to end
#define LONG_US (3 * TEST_WAIT_US)

// The UID each waiter waits through, and its wait mode and timeout for an event flag
static SceUID through[2];
static SceUInt32 wait_mode[2];
static SceUInt wait_us[2];
static int results[2];
static SceUInt32 bits[2];
static SceUInt64 user_data[2];

// Waits for bit 0x1 of the event flag through the UID of waiter i, where i is the argument
static int flag_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = wait_us[i];
    results[i] = sceKernelWaitEventFlag(through[i], 0x1, wait_mode[i], &bits[i], &timeout);
    return 0;
}

// Waits for A on the simple event through the UID of waiter i, where i is the argument
static int event_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = wait_us[i];
    results[i] = sceKernelWaitEvent(through[i], EVENT_BIT_A, &bits[i], &user_data[i], &timeout);
    return 0;
}

// Returns whether thread waits with the wait type, through whichever UID it waits on
static int waiting(SceUID thread, SceUInt32 type) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType == type;
}

// Starts waiter i through uid on entry, and returns once it waits with the wait type
static SceUID start_waiter(int i, SceKernelThreadEntry entry, SceUID uid, SceUInt32 type) {
    through[i] = uid;
    results[i] = NOT_DONE;
    bits[i] = NOT_DONE;
    user_data[i] = NOT_DONE;
    const SceUID thread = sceKernelCreateThread(
        "waiter", entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, sizeof(i), &i);
    TEST_WAIT_FOR(waiting(thread, type));
    return thread;
}

// Returns the waiting threads the info call of the event flag through uid counts, or its error
static int flag_waiting(SceUID uid) {
    SceKernelEventFlagInfo info = { .size = sizeof(info) };
    const int result = sceKernelGetEventFlagInfo(uid, &info);
    return result < 0 ? result : (int)info.numWaitThreads;
}

// Returns the waiting threads the info call of the simple event through uid counts, or its error
static int event_waiting(SceUID uid) {
    EventInfo info = { .size = sizeof(info) };
    const int result = sceKernelGetEventInfo(uid, &info);
    return result < 0 ? result : (int)info.numWaitThreads;
}

// Returns the pattern of the event flag through uid, or NOT_DONE if the info call fails
static SceUInt32 flag_pattern(SceUID uid) {
    SceKernelEventFlagInfo info = { .size = sizeof(info) };
    return sceKernelGetEventFlagInfo(uid, &info) < 0 ? NOT_DONE : info.currentPattern;
}

static void multi_flag_case(void) {
    const char *name = "multiple waiter flag";
    SceUID threads[2];
    const SceUID id = sceKernelCreateEventFlag(
        name, SCE_KERNEL_ATTR_OPENABLE | SCE_EVENT_WAITMULTIPLE, 0, NULL);
    const SceUID opened = sceKernelOpenEventFlag(name);
    PRINTF(id < 0 ? id : opened < 0 ? opened : 0, "%s: create and open", name);

    wait_mode[0] = SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR;
    wait_us[0] = LONG_US;
    threads[0] = start_waiter(0, flag_waiter, id, WAIT_TYPE_EVENT_FLAG);
    wait_mode[1] = SCE_EVENT_WAITOR;
    wait_us[1] = TIMEOUT_US;
    threads[1] = start_waiter(1, flag_waiter, opened, WAIT_TYPE_EVENT_FLAG);

    PRINTF(flag_waiting(id), "%s: the waiting threads through the creating UID", name);
    PRINTF(flag_waiting(opened), "%s: the waiting threads through the opened UID", name);
    PRINTF(sceKernelSetEventFlag(opened, 0x1), "%s: the set through the opened UID", name);
    PRINTF(flag_waiting(id), "%s: the waiting threads after the set, creating UID", name);
    PRINTF(flag_waiting(opened), "%s: the waiting threads after the set, opened UID", name);
    PRINTF(flag_pattern(id), "%s: the pattern after the set", name);

    test_join_thread(threads[0]);
    test_join_thread(threads[1]);
    PRINTF(results[0], "%s: W0's result", name);
    PRINTF(bits[0], "%s: the bits W0 got", name);
    PRINTF(results[1], "%s: W1's result", name);
    sceKernelCloseEventFlag(opened);
    sceKernelDeleteEventFlag(id);
}

static void single_flag_case(void) {
    const char *name = "single waiter flag";
    SceUID threads[1];
    SceUInt32 got = NOT_DONE;
    SceUInt timeout = 1000;
    const SceUID id = sceKernelCreateEventFlag(name, SCE_KERNEL_ATTR_OPENABLE, 0, NULL);
    const SceUID opened = sceKernelOpenEventFlag(name);
    PRINTF(id < 0 ? id : opened < 0 ? opened : 0, "%s: create and open", name);

    wait_mode[0] = SCE_EVENT_WAITOR;
    wait_us[0] = LONG_US;
    threads[0] = start_waiter(0, flag_waiter, id, WAIT_TYPE_EVENT_FLAG);

    PRINTF(sceKernelWaitEventFlag(opened, 0x1, SCE_EVENT_WAITOR, &got, &timeout),
        "%s: a 1 ms wait through the opened UID", name);
    PRINTF(sceKernelPollEventFlag(opened, 0x1, SCE_EVENT_WAITOR, &got),
        "%s: a poll through the opened UID", name);
    PRINTF(flag_waiting(opened), "%s: the waiting threads through the opened UID", name);
    PRINTF(sceKernelSetEventFlag(opened, 0x1), "%s: the set through the opened UID", name);
    PRINTF(flag_waiting(id), "%s: the waiting threads after the set", name);
    PRINTF(sceKernelPollEventFlag(opened, 0x1, SCE_EVENT_WAITOR, &got),
        "%s: a poll through the opened UID after the set", name);

    test_join_thread(threads[0]);
    PRINTF(results[0], "%s: W0's result", name);
    PRINTF(bits[0], "%s: the bits W0 got", name);
    sceKernelCloseEventFlag(opened);
    sceKernelDeleteEventFlag(id);
}

static void simple_event_case(void) {
    const char *name = "auto reset event";
    SceUID threads[2];
    SceUInt32 pattern = NOT_DONE;
    const SceUID id = sceKernelCreateSimpleEvent(
        name, SCE_KERNEL_ATTR_OPENABLE | EVENT_ATTR_AUTO_RESET, 0, NULL);
    const SceUID opened = sceKernelOpenSimpleEvent(name);
    PRINTF(id < 0 ? id : opened < 0 ? opened : 0, "%s: create and open", name);

    wait_us[0] = LONG_US;
    threads[0] = start_waiter(0, event_waiter, id, WAIT_TYPE_EVENT);
    wait_us[1] = LONG_US;
    threads[1] = start_waiter(1, event_waiter, opened, WAIT_TYPE_EVENT);

    PRINTF(event_waiting(id), "%s: the waiting threads through the creating UID", name);
    PRINTF(event_waiting(opened), "%s: the waiting threads through the opened UID", name);
    PRINTF(sceKernelSetEvent(opened, EVENT_BIT_A, 0x11), "%s: the set through the opened UID",
        name);
    PRINTF(event_waiting(id), "%s: the waiting threads after the set, creating UID", name);
    PRINTF(event_waiting(opened), "%s: the waiting threads after the set, opened UID", name);
    sceKernelGetEventPattern(id, &pattern);
    PRINTF(pattern & EVENT_BIT_A, "%s: the bit A after the set", name);
    PRINTF(sceKernelSetEvent(id, EVENT_BIT_A, 0x22), "%s: the second set through the creating UID",
        name);
    PRINTF(event_waiting(id), "%s: the waiting threads after the second set", name);

    test_join_thread(threads[0]);
    test_join_thread(threads[1]);
    PRINTF(results[0], "%s: W0's result", name);
    PRINTF(bits[0] & EVENT_BIT_A, "%s: the bits W0 got", name);
    PRINTF((SceUInt32)user_data[0], "%s: the low word of the user data W0 got", name);
    PRINTF(results[1], "%s: W1's result", name);
    PRINTF(bits[1] & EVENT_BIT_A, "%s: the bits W1 got", name);
    PRINTF((SceUInt32)user_data[1], "%s: the low word of the user data W1 got", name);
    sceKernelCloseSimpleEvent(opened);
    sceKernelDeleteSimpleEvent(id);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    multi_flag_case();
    single_flag_case();
    simple_event_case();

    test_exit();
    return 0;
}
