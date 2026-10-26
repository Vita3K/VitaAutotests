// What a wait whose timeout passed leaves behind in the object it waited on, while its thread
// hasn't run yet, and what a later set, poll or wait finds there. The main thread runs on core 0
// at 0x60 and every waiter is pinned to core 0 at 0xB0, so a waiter whose wait ended stays ready
// until the main thread blocks. A waiter that should time out gets a timeout of 300 ms. The main
// thread then spins for a second on the process clock, with no call that blocks, so the deadline
// passes while it runs. Right after the spin it reads the state, then makes the call of the case,
// and only then joins the threads and prints. The status is 2 for a ready thread and 8 for a
// waiting one. Every wait that a set is meant to end gets a timeout of 6 seconds, far past the
// spin. Each case uses new objects and threads. The cases:
// - an auto reset simple event, where A waits for A_BIT with the short timeout and B queues behind
//   it for the same bit. The set of A_BIT after the spin checks whether A is skipped and B woken
// - the same with only A. The test checks whether the set leaves the bit, and what a poll
//   afterwards gets, with its user data
// - a manual reset simple event with A and B as above. The test checks whether B wakes and the bit
//   stays
// - a flag with the multiple waiter attribute, where A waits for bit 0x1 with OR and the short
//   timeout, and B waits for it with OR and a clear, queued behind A. The test checks the bits A
//   gets, which are the bits the flag held at its deadline
// - a set made before the deadline, with a timeout of 1 s and a spin of 1.6 s after the set. This
//   is done for a simple event, a flag and an OR multiple wait. The test checks the result, bits
//   and user data the set gave, and whether the time left is above 500 ms
// - an OR multiple wait on two auto reset events with the short timeout. The test checks how many
//   nodes are still queued after the spin, and what a set of E1 does to them, with a plain waiter
//   for E1 queued behind the multiple wait and with none
// Each case prints the states the main thread read, then what each waiter got once it has ended.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define USER_DATA 0x1234
#define MULTI_EVENTS_WAIT_ID ((SceUID)0x80000000)
// The timeout of a waiter that the spin lets time out, and the spins
#define SHORT_US 300000
#define SPIN_US 1000000
#define ONE_SECOND_US 1000000
#define LONG_SPIN_US 1600000
// The timeout of a waiter that a set or a poll ends, far past the spins
#define LONG_US (3 * TEST_WAIT_US)
#define HALF_SECOND_US 500000

typedef enum Kind { SIMPLE_EVENT, FLAG, MULTI_EVENTS } Kind;

static SceUID event, event2, flag;
// The timeouts, results, bits and user data of the two waiters of a case, by index
static SceUInt timeouts[2];
static volatile int results[2];
static volatile SceUInt32 patterns[2];
static volatile SceUInt64 user_datas[2];
// The mode of each flag waiter: A waits with OR, B with OR and a clear
static const SceUInt32 flag_modes[2] = { SCE_EVENT_WAITOR, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR };

static Kind kind;
static WaitEvent wait_events[2];
static ResultEvent entries[2];
static volatile int wait_result;
static volatile SceUInt32 wait_bits, wait_time_left;
static volatile SceUInt64 wait_user_data;

static volatile int multi_result, plain_result;
static volatile SceUInt32 plain_bits;
static volatile SceUInt64 plain_user_data;

// Starts entry on core 0 at the waiter priority, passing it arg
static SceUID start(SceKernelThreadEntry entry, int arg) {
    const SceUID thread = sceKernelCreateThread("w", entry, WAITER_PRIORITY, 0x4000, 0,
        SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, sizeof(arg), &arg);
    return thread;
}

// Spins on the process clock, so the main thread keeps core 0 and never blocks
static void spin(SceUInt64 us) {
    const SceUInt64 end = sceKernelGetProcessTimeWide() + us;
    while (sceKernelGetProcessTimeWide() < end)
        ;
}

// Returns the status sceKernelGetThreadInfo reports for thread
static int status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

// Returns the info of a simple event
static EventInfo info_of(SceUID id) {
    EventInfo info = { .size = sizeof(info) };
    sceKernelGetEventInfo(id, &info);
    return info;
}

// Returns the A and B bits of a simple event's pattern, without the other bits the pattern holds
static SceUInt32 user_bits_of(SceUID id) {
    return info_of(id).eventPattern & (EVENT_BIT_A | EVENT_BIT_B);
}

// Returns the info of the flag
static SceKernelEventFlagInfo flag_info(void) {
    SceKernelEventFlagInfo info = { .size = sizeof(info) };
    sceKernelGetEventFlagInfo(flag, &info);
    return info;
}

// Returns whether thread waits on the object of kind k
static int waiting_in(SceUID thread, Kind k) {
    if (k == FLAG)
        return test_in_wait(thread, WAIT_TYPE_EVENT_FLAG, flag);
    if (k == SIMPLE_EVENT)
        return test_in_wait(thread, WAIT_TYPE_EVENT, event);
    return test_in_wait(thread, WAIT_TYPE_MP_EVENTS, MULTI_EVENTS_WAIT_ID);
}

// Waits for A_BIT on the event with the timeout of the index passed in argument
static int pair_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt32 pattern = NOT_DONE;
    SceUInt64 user_data = NOT_DONE;
    SceUInt timeout = timeouts[i];
    results[i] = sceKernelWaitEvent(event, EVENT_BIT_A, &pattern, &user_data, &timeout);
    patterns[i] = pattern;
    user_datas[i] = user_data;
    return 0;
}

// Starts waiter i of the pair and returns once it waits on the event
static SceUID start_pair_waiter(int i) {
    const SceUID thread = start(pair_waiter, i);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_EVENT, event));
    return thread;
}

// Two waiters for A_BIT on a simple event of the given attribute: A with the short timeout, and B
// queued behind it with the long one. The spin lets A time out, then A_BIT is set.
static void pair_case(const char *name, SceUInt32 attr) {
    event = sceKernelCreateSimpleEvent("event", attr, 0, NULL);
    timeouts[0] = SHORT_US;
    timeouts[1] = LONG_US;
    for (int i = 0; i < 2; i++) {
        results[i] = NOT_DONE;
        patterns[i] = NOT_DONE;
        user_datas[i] = NOT_DONE;
    }
    const SceUID a = start_pair_waiter(0);
    const SceUID b = start_pair_waiter(1);

    spin(SPIN_US);
    const int a_status = status_of(a);
    const int b_status = status_of(b);
    const int waiting = info_of(event).numWaitThreads;
    const int set = sceKernelSetEvent(event, EVENT_BIT_A, USER_DATA);
    const SceUInt32 left = user_bits_of(event);
    test_join_thread(a);
    test_join_thread(b);

    PRINTF(a_status, "%s: A's status after its deadline", name);
    PRINTF(b_status, "%s: B's status after A's deadline", name);
    PRINTF(waiting, "%s: the waiters after A's deadline", name);
    PRINTF(set, "%s: the set", name);
    PRINTF(left, "%s: the pattern after the set", name);
    PRINTF(results[0], "%s: A's result", name);
    PRINTF(patterns[0], "%s: A's pattern", name);
    PRINTF((SceUInt32)user_datas[0], "%s: A's user data", name);
    PRINTF(results[1], "%s: B's result", name);
    PRINTF(patterns[1], "%s: B's pattern", name);
    PRINTF((SceUInt32)user_datas[1], "%s: B's user data", name);
    sceKernelDeleteSimpleEvent(event);
}

// One waiter for A_BIT with the short timeout, which the spin lets time out. The set then has no
// waiter to serve, and a poll afterwards reads the bit and the user data of the set.
static void alone_case(void) {
    const char *name = "auto reset simple event, A alone";
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL);
    timeouts[0] = SHORT_US;
    results[0] = NOT_DONE;
    patterns[0] = NOT_DONE;
    user_datas[0] = NOT_DONE;
    const SceUID a = start_pair_waiter(0);

    spin(SPIN_US);
    const int a_status = status_of(a);
    const int waiting = info_of(event).numWaitThreads;
    const int set = sceKernelSetEvent(event, EVENT_BIT_A, USER_DATA);
    const SceUInt32 left = user_bits_of(event);
    SceUInt32 poll_bits = NOT_DONE;
    SceUInt64 poll_user_data = NOT_DONE;
    const int poll = sceKernelPollEvent(event, EVENT_BIT_A, &poll_bits, &poll_user_data);
    const SceUInt32 after_poll = user_bits_of(event);
    test_join_thread(a);

    PRINTF(a_status, "%s: A's status after its deadline", name);
    PRINTF(waiting, "%s: the waiters after A's deadline", name);
    PRINTF(set, "%s: the set", name);
    PRINTF(left, "%s: the pattern after the set", name);
    PRINTF(poll, "%s: the poll", name);
    PRINTF(poll_bits, "%s: the bits the poll got", name);
    PRINTF((SceUInt32)poll_user_data, "%s: the low word of the user data the poll got", name);
    PRINTF(after_poll, "%s: the pattern after the poll", name);
    PRINTF(results[0], "%s: A's result", name);
    PRINTF(patterns[0], "%s: A's pattern", name);
    PRINTF((SceUInt32)user_datas[0], "%s: A's user data", name);
    sceKernelDeleteSimpleEvent(event);
}

// Waits for bit 0x1 of the flag with the mode and timeout of the index passed in argument
static int flag_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt32 bits = NOT_DONE;
    SceUInt timeout = timeouts[i];
    results[i] = sceKernelWaitEventFlag(flag, 0x1, flag_modes[i], &bits, &timeout);
    patterns[i] = bits;
    return 0;
}

// Two waiters for bit 0x1 of a flag with the multiple waiter attribute: A with the short timeout,
// and B with the long one, queued behind A and clearing the bit when it takes it. The spin lets A
// time out, then the flag is set.
static void flag_case(void) {
    const char *name = "flag with the multiple waiter attribute";
    flag = sceKernelCreateEventFlag("flag", SCE_EVENT_WAITMULTIPLE, 0, NULL);
    timeouts[0] = SHORT_US;
    timeouts[1] = LONG_US;
    for (int i = 0; i < 2; i++) {
        results[i] = NOT_DONE;
        patterns[i] = NOT_DONE;
    }
    const SceUID a = start(flag_waiter, 0);
    TEST_WAIT_FOR(test_in_wait(a, WAIT_TYPE_EVENT_FLAG, flag));
    const SceUID b = start(flag_waiter, 1);
    TEST_WAIT_FOR(test_in_wait(b, WAIT_TYPE_EVENT_FLAG, flag));

    spin(SPIN_US);
    const int a_status = status_of(a);
    const int b_status = status_of(b);
    const int waiting = flag_info().numWaitThreads;
    const int set = sceKernelSetEventFlag(flag, 0x1);
    test_join_thread(a);
    test_join_thread(b);
    const SceUInt32 left = flag_info().currentPattern;

    PRINTF(a_status, "%s: A's status after its deadline", name);
    PRINTF(b_status, "%s: B's status after A's deadline", name);
    PRINTF(waiting, "%s: the waiters after A's deadline", name);
    PRINTF(set, "%s: the set", name);
    PRINTF(results[0], "%s: A's result", name);
    PRINTF(patterns[0], "%s: A's out bits", name);
    PRINTF(results[1], "%s: B's result", name);
    PRINTF(patterns[1], "%s: B's out bits", name);
    PRINTF(left, "%s: the pattern after both", name);
    sceKernelDeleteEventFlag(flag);
}

// Waits on the object of kind k with a timeout of one second, and records the out values
static int set_first_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = ONE_SECOND_US;
    SceUInt32 bits = NOT_DONE;
    SceUInt64 user_data = NOT_DONE;
    switch (kind) {
    case SIMPLE_EVENT:
        wait_result = sceKernelWaitEvent(event, EVENT_BIT_A, &bits, &user_data, &timeout);
        break;
    case FLAG:
        wait_result = sceKernelWaitEventFlag(flag, 0x1, SCE_EVENT_WAITOR, &bits, &timeout);
        break;
    case MULTI_EVENTS:
        wait_result =
            sceKernelWaitMultipleEvents(wait_events, 2, EVENT_WAIT_MODE_OR, entries, &timeout);
        bits = entries[0].resultPattern;
        user_data = entries[0].userData;
        break;
    }
    wait_bits = bits;
    wait_user_data = user_data;
    wait_time_left = timeout;
    return 0;
}

// Waits on the object of kind k with a timeout of one second, then the main thread sets it at once
// and spins 1.6 s, so the deadline passes after the set
static void set_first_case(const char *name, Kind k) {
    kind = k;
    wait_result = NOT_DONE;
    wait_bits = NOT_DONE;
    wait_user_data = NOT_DONE;
    wait_time_left = 0;
    sceClibMemset(entries, 0xAA, sizeof(entries));
    if (k == FLAG) {
        flag = sceKernelCreateEventFlag("flag", 0, 0, NULL);
    } else {
        event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL);
        wait_events[0] = (WaitEvent){ event, EVENT_BIT_A };
        if (k == MULTI_EVENTS) {
            event2 = sceKernelCreateSimpleEvent("event 2", EVENT_ATTR_AUTO_RESET, 0, NULL);
            wait_events[1] = (WaitEvent){ event2, EVENT_BIT_A };
        }
    }
    const SceUID w = start(set_first_waiter, 0);
    TEST_WAIT_FOR(waiting_in(w, k));

    const int set = k == FLAG ? sceKernelSetEventFlag(flag, 0x1)
                              : sceKernelSetEvent(event, EVENT_BIT_A, USER_DATA);
    spin(LONG_SPIN_US);
    test_join_thread(w);

    PRINTF(set, "%s: the set", name);
    PRINTF(wait_result, "%s: the wait", name);
    if (k == MULTI_EVENTS)
        PRINTF(entries[0].result, "%s: the entry for E1's result", name);
    PRINTF(wait_bits, "%s: the bits", name);
    if (k != FLAG)
        PRINTF((SceUInt32)wait_user_data, "%s: the low word of the user data", name);
    PRINTF(wait_time_left > HALF_SECOND_US, "%s: the time left is above 500 ms", name);
    if (k == FLAG) {
        sceKernelDeleteEventFlag(flag);
    } else {
        sceKernelDeleteSimpleEvent(event);
        if (k == MULTI_EVENTS)
            sceKernelDeleteSimpleEvent(event2);
    }
}

// The OR wait on E1 and E2 for A, with the short timeout
static int multi_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = SHORT_US;
    multi_result =
        sceKernelWaitMultipleEvents(wait_events, 2, EVENT_WAIT_MODE_OR, entries, &timeout);
    return 0;
}

// The plain wait for A on E1, with the long timeout
static int plain_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 pattern = NOT_DONE;
    SceUInt64 user_data = NOT_DONE;
    SceUInt32 timeout = LONG_US;
    plain_result = sceKernelWaitEvent(event, EVENT_BIT_A, &pattern, &user_data, &timeout);
    plain_bits = pattern;
    plain_user_data = user_data;
    return 0;
}

// The OR multiple wait on E1 and E2 has the short timeout, which the spin lets pass. The main
// thread then sets A on E1, with a plain waiter for E1 queued behind the multiple wait, or with
// none.
static void multi_case(const char *name, int with_plain) {
    event = sceKernelCreateSimpleEvent("E1", EVENT_ATTR_AUTO_RESET, 0, NULL);
    event2 = sceKernelCreateSimpleEvent("E2", EVENT_ATTR_AUTO_RESET, 0, NULL);
    wait_events[0] = (WaitEvent){ event, EVENT_BIT_A };
    wait_events[1] = (WaitEvent){ event2, EVENT_BIT_A };
    sceClibMemset(entries, 0xAA, sizeof(entries));
    multi_result = NOT_DONE;
    plain_result = NOT_DONE;
    plain_bits = NOT_DONE;
    plain_user_data = NOT_DONE;

    const SceUID w = start(multi_waiter, 0);
    TEST_WAIT_FOR(test_in_wait(w, WAIT_TYPE_MP_EVENTS, MULTI_EVENTS_WAIT_ID));
    SceUID s = 0;
    if (with_plain) {
        s = start(plain_waiter, 0);
        TEST_WAIT_FOR(test_in_wait(s, WAIT_TYPE_EVENT, event));
    }

    spin(SPIN_US);
    const int w_status = status_of(w);
    const int s_status = with_plain ? status_of(s) : NOT_DONE;
    const int before_e1 = info_of(event).numWaitThreads;
    const int before_e2 = info_of(event2).numWaitThreads;
    const int set = sceKernelSetEvent(event, EVENT_BIT_A, USER_DATA);
    const int after_e1 = info_of(event).numWaitThreads;
    const SceUInt32 after_e1_pattern = user_bits_of(event);
    const int after_e2 = info_of(event2).numWaitThreads;
    if (with_plain)
        test_join_thread(s);
    test_join_thread(w);
    const int end_e1 = info_of(event).numWaitThreads;
    const SceUInt32 end_e1_pattern = user_bits_of(event);
    const int end_e2 = info_of(event2).numWaitThreads;
    const SceUInt32 end_e2_pattern = user_bits_of(event2);

    PRINTF(w_status, "%s: the multiple wait's status after the spin", name);
    if (with_plain)
        PRINTF(s_status, "%s: the plain waiter's status after the spin", name);
    PRINTF(before_e1, "%s: E1's waiters after the spin", name);
    PRINTF(before_e2, "%s: E2's waiters after the spin", name);
    PRINTF(set, "%s: the set of E1", name);
    PRINTF(after_e1, "%s: E1's waiters after the set", name);
    PRINTF(after_e1_pattern, "%s: E1's pattern after the set", name);
    PRINTF(after_e2, "%s: E2's waiters after the set", name);
    PRINTF(multi_result, "%s: the multiple wait", name);
    // A timed out wait may leave any value in the result words, so print whether they changed
    PRINTF(entries[0].result == (SceInt32)0xAAAAAAAA, "%s: the multiple wait's entry 0 result is left as it was", name);
    PRINTF(entries[1].result == (SceInt32)0xAAAAAAAA, "%s: the multiple wait's entry 1 result is left as it was", name);
    if (with_plain) {
        PRINTF(plain_result, "%s: the plain waiter's result", name);
        PRINTF(plain_bits, "%s: the plain waiter's bits", name);
        PRINTF((SceUInt32)plain_user_data, "%s: the low word of the plain waiter's data", name);
    }
    PRINTF(end_e1, "%s: E1's waiters at the end", name);
    PRINTF(end_e1_pattern, "%s: E1's pattern at the end", name);
    PRINTF(end_e2, "%s: E2's waiters at the end", name);
    PRINTF(end_e2_pattern, "%s: E2's pattern at the end", name);
    sceKernelDeleteSimpleEvent(event);
    sceKernelDeleteSimpleEvent(event2);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    pair_case("auto reset simple event, A and B", EVENT_ATTR_AUTO_RESET);
    alone_case();
    pair_case("manual reset simple event, A and B", EVENT_ATTR_MANUAL_RESET);
    flag_case();
    set_first_case("set before the deadline, simple event", SIMPLE_EVENT);
    set_first_case("set before the deadline, flag", FLAG);
    set_first_case("set before the deadline, OR multiple wait", MULTI_EVENTS);
    multi_case("OR multiple wait, plain waiter for E1 behind it", 1);
    multi_case("OR multiple wait, no waiter behind it", 0);

    test_exit();
    return 0;
}
