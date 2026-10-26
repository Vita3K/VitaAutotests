// A wait that can take its bits at once does so while another thread is queued on the same object
// for bits that aren't set. Main waits for bits that are set, as a plain wait and as a callback
// wait, and the waiter queued ahead of it keeps waiting. Every thread is pinned to core 0. Main is
// at 0x60 and the waiter at 0xB0, except in the priority cases, where the waiter is at 0x50, above
// main. The cases:
// - a multiple event flag holding 0x1, a waiter for AND 0x3, and main's wait for OR 0x1 with
//   CLEAR_PAT
// - the same with the priority queue attribute, and the waiter above main
// - a manual reset simple event with A set, a waiter queued for B, and main's wait for A
// - the same with an auto reset simple event
// Each case prints the result of main's wait, whether it returned within 1 s, the bits and the
// user data it got, whether the waiter is still waiting, the number of waiters, and the pattern.
// Then the waiter is released with B or 0x3, and its result and bits are printed.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define ABOVE_MAIN_PRIORITY 0x50

// Main's wait and the waiter's, both with a timeout of three TEST_WAIT_US
#define MAIN_TIMEOUT (3 * TEST_WAIT_US)
#define USER_BITS (EVENT_BIT_A | EVENT_BIT_B)

static SceUID object;
static volatile int waiter_result;
static volatile SceUInt32 waiter_bits;
static volatile SceUInt64 waiter_data;

static int flag_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 bits = 0xDEAD;
    SceUInt32 timeout = 3 * TEST_WAIT_US;
    waiter_result = sceKernelWaitEventFlag(object, 0x3, SCE_EVENT_WAITAND, &bits, &timeout);
    waiter_bits = bits;
    return 0;
}

static int event_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 bits = 0xDEAD;
    SceUInt64 user_data = 0xDEAD;
    SceUInt32 timeout = 3 * TEST_WAIT_US;
    waiter_result = sceKernelWaitEvent(object, EVENT_BIT_B, &bits, &user_data, &timeout);
    waiter_bits = bits;
    waiter_data = user_data;
    return 0;
}

// Starts entry on a new thread at priority, pinned to core 0
static SceUID start_waiter(SceKernelThreadEntry entry, int priority) {
    const SceUID thread = sceKernelCreateThread(
        "waiter", entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

// A multiple event flag holding 0x1, with a waiter queued for 0x3 with AND, and main taking 0x1
static void flag_case(const char *name, SceUInt32 attr, int priority, int callbacks) {
    object = sceKernelCreateEventFlag("flag", SCE_EVENT_WAITMULTIPLE | attr, 0x1, NULL);
    waiter_result = 0xDEAD;
    waiter_bits = 0xDEAD;
    const SceUID a = start_waiter(flag_waiter, priority);
    TEST_WAIT_FOR(test_in_wait(a, WAIT_TYPE_EVENT_FLAG, object));

    const SceUInt32 mode = SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT;
    SceUInt32 bits = 0xDEAD;
    SceUInt32 timeout = MAIN_TIMEOUT;
    int result;
    if (callbacks)
        result = sceKernelWaitEventFlagCB(object, 0x1, mode, &bits, &timeout);
    else
        result = sceKernelWaitEventFlag(object, 0x1, mode, &bits, &timeout);
    const int in_time = timeout > MAIN_TIMEOUT - 1000000;
    const int a_waiting = test_in_wait(a, WAIT_TYPE_EVENT_FLAG, object);
    SceKernelEventFlagInfo info = { .size = sizeof(info) };
    sceKernelGetEventFlagInfo(object, &info);
    const SceUInt32 pattern = info.currentPattern;
    const int count = info.numWaitThreads;

    sceKernelSetEventFlag(object, 0x3);
    test_join_thread(a);
    PRINTF(result, "%s: the wait's result", name);
    PRINTF(in_time, "%s: the wait returned within 1 s", name);
    PRINTF(bits, "%s: the bits the wait got", name);
    PRINTF(a_waiting, "%s: the waiter is still waiting", name);
    PRINTF(count, "%s: the number of waiters", name);
    PRINTF(pattern, "%s: the pattern", name);
    PRINTF(waiter_result, "%s: the waiter's result", name);
    PRINTF(waiter_bits, "%s: the bits the waiter got", name);
    sceKernelDeleteEventFlag(object);
}

// A simple event with A set, a waiter queued for B, and main taking A
static void event_case(const char *name, SceUInt32 attr, int callbacks) {
    object = sceKernelCreateSimpleEvent("event", attr, 0, NULL);
    waiter_result = 0xDEAD;
    waiter_bits = 0xDEAD;
    waiter_data = 0xDEAD;
    const SceUID a = start_waiter(event_waiter, WAITER_PRIORITY);
    TEST_WAIT_FOR(test_in_wait(a, WAIT_TYPE_EVENT, object));

    const int set = sceKernelSetEvent(object, EVENT_BIT_A, 0x1234);
    SceUInt32 bits = 0xDEAD;
    SceUInt64 user_data = 0xDEAD;
    SceUInt32 timeout = MAIN_TIMEOUT;
    int result;
    if (callbacks)
        result = sceKernelWaitEventCB(object, EVENT_BIT_A, &bits, &user_data, &timeout);
    else
        result = sceKernelWaitEvent(object, EVENT_BIT_A, &bits, &user_data, &timeout);
    const int in_time = timeout > MAIN_TIMEOUT - 1000000;
    const int a_waiting = test_in_wait(a, WAIT_TYPE_EVENT, object);
    EventInfo info = { .size = sizeof(info) };
    sceKernelGetEventInfo(object, &info);
    SceUInt32 pattern = 0xDEAD;
    sceKernelGetEventPattern(object, &pattern);

    sceKernelSetEvent(object, EVENT_BIT_B, 0x77);
    test_join_thread(a);
    PRINTF(set, "%s: the set of A", name);
    PRINTF(result, "%s: the wait's result", name);
    PRINTF(in_time, "%s: the wait returned within 1 s", name);
    PRINTF(bits & USER_BITS, "%s: the bits the wait got", name);
    PRINTF((SceUInt32)user_data, "%s: the user data the wait got", name);
    PRINTF(a_waiting, "%s: the waiter is still waiting", name);
    PRINTF(info.numWaitThreads, "%s: the number of waiters", name);
    PRINTF(pattern & USER_BITS, "%s: the pattern", name);
    PRINTF(waiter_result, "%s: the waiter's result", name);
    PRINTF(waiter_bits & USER_BITS, "%s: the bits the waiter got", name);
    PRINTF((SceUInt32)waiter_data, "%s: the user data the waiter got", name);
    sceKernelDeleteSimpleEvent(object);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    flag_case("multi flag, plain wait", 0, WAITER_PRIORITY, 0);
    flag_case("multi flag, callback wait", 0, WAITER_PRIORITY, 1);
    flag_case("priority multi flag, plain wait", SCE_KERNEL_ATTR_THREAD_PRIO,
        ABOVE_MAIN_PRIORITY, 0);
    flag_case("priority multi flag, callback wait", SCE_KERNEL_ATTR_THREAD_PRIO,
        ABOVE_MAIN_PRIORITY, 1);
    event_case("manual reset simple event, plain wait", EVENT_ATTR_MANUAL_RESET, 0);
    event_case("manual reset simple event, callback wait", EVENT_ATTR_MANUAL_RESET, 1);
    event_case("auto reset simple event, plain wait", EVENT_ATTR_AUTO_RESET, 0);
    event_case("auto reset simple event, callback wait", EVENT_ATTR_AUTO_RESET, 1);

    test_exit();
    return 0;
}
