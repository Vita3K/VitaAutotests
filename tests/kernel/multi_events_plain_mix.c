// Plain and multiple waits on the same simple events: what a set or a pulse records for each kind
// of wait, and how a multiple wait node that a set passes by counts. Every thread runs on core 0,
// the waiters at 0xB0 below the main thread at 0x60, so a waiter that a set woke can't run before
// the main thread blocks. The main thread reads what it needs before it joins the waiters, and
// prints only after the joins. The cases:
// - manual reset e1 with a plain wait P for A queued first, then a multiple wait T with OR on e1
//   (A) and e2 (B). A set of e1 serves both and the bit stays. A pulse of e1 after that clears the
//   bit.
// - T lists e1 with A or C and e2 with B, and e1 is set with A alone. The entry records A only.
//   The same again with A and D set, where D isn't in T's wait, and the entry still records A only.
// - auto reset e1 and e2, T waits alone with OR on e1 (A) and e2 (B). A set of e1 claims T before
//   it runs, and the info counts are read. A set of e2 passes T's e2 node by, and the counts are
//   read again. The e2 bit stays, a poll takes it with its user data, and T ends with one entry.
// - T waits with OR on e1 (A and the delete bit 0x8) and e2 (B), and P waits for A on e1 before
//   it. e1 is deleted. T's entry is for e1 with bits 0x8 and result 0, and P gets the delete
//   result.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define MULTI_EVENTS_WAIT_ID ((SceUID)0x80000000)

// Each wait gets three times the test's wait limit, so a wait that stays queued still ends
#define WAIT_LIMIT (3 * TEST_WAIT_US)

static SceUID e1, e2;
static WaitEvent list[2];
static ResultEvent results[2];
static SceUInt32 multi_mode;
static volatile int multi_result;
static volatile int multi_returned;
static volatile int plain_result;
static SceUInt32 plain_pattern;
static SceUInt64 plain_user_data;

// Waits on the list with the mode of the case
static int multi_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = WAIT_LIMIT;
    multi_result = sceKernelWaitMultipleEvents(list, 2, multi_mode, results, &timeout);
    multi_returned = 1;
    return 0;
}

// Waits for EVENT_BIT_A of e1 alone
static int plain_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = WAIT_LIMIT;
    plain_result = sceKernelWaitEvent(e1, EVENT_BIT_A, &plain_pattern, &plain_user_data, &timeout);
    return 0;
}

// Starts the plain waiter on core 0, and returns once it is in its wait
static SceUID start_plain(void) {
    const SceUID thread = sceKernelCreateThread("plain", plain_waiter, WAITER_PRIORITY, 0x4000, 0,
                                                SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_EVENT, e1));
    return thread;
}

// Starts the multiple waiter on core 0, and returns once it is in its wait or has returned
static SceUID start_multi(void) {
    const SceUID thread = sceKernelCreateThread("multi", multi_waiter, WAITER_PRIORITY, 0x4000, 0,
                                                SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_MP_EVENTS, MULTI_EVENTS_WAIT_ID) ||
                  multi_returned);
    return thread;
}

// Creates e1 and e2 with attr, lists e1_bits on e1 and B on e2 for the multiple wait
static void setup(SceUInt32 attr, SceUInt32 e1_bits, SceUInt32 mode) {
    e1 = sceKernelCreateSimpleEvent("e1", attr, 0, NULL);
    e2 = sceKernelCreateSimpleEvent("e2", attr, 0, NULL);
    list[0] = (WaitEvent){ e1, e1_bits };
    list[1] = (WaitEvent){ e2, EVENT_BIT_B };
    multi_mode = mode;
    multi_result = 0xDEAD;
    multi_returned = 0;
    plain_result = 0xDEAD;
    plain_pattern = 0xDEAD;
    plain_user_data = 0xDEAD;
    sceClibMemset(results, 0xAA, sizeof(results));
}

static void teardown(void) {
    sceKernelDeleteSimpleEvent(e1);
    sceKernelDeleteSimpleEvent(e2);
}

// Returns the pattern of the event, or 0xDEAD if it can't be read
static SceUInt32 pattern_of(SceUID event) {
    SceUInt32 pattern = 0xDEAD;
    sceKernelGetEventPattern(event, &pattern);
    return pattern;
}

// Returns the number of threads waiting on the event, as its info reports them
static SceUInt32 waiters_of(SceUID event) {
    EventInfo info = { .size = sizeof(info), .numWaitThreads = 0xDEAD };
    sceKernelGetEventInfo(event, &info);
    return info.numWaitThreads;
}

// Prints the entries a multiple wait filled, for the count its call returned
static void print_entries(const char *name, int count) {
    for (int i = 0; i < 2 && i < count; i++) {
        PRINTF(results[i].eventId == e1 ? 1 : results[i].eventId == e2 ? 2 : 0,
               "%s: entry %d is for event 1 or 2", name, i);
        PRINTF(results[i].result, "%s: entry %d result", name, i);
        PRINTF(results[i].resultPattern, "%s: entry %d bits", name, i);
        PRINTF((int)results[i].userData, "%s: entry %d user data", name, i);
    }
}

// A plain wait and a multiple wait on manual reset e1 are both served by one set, and the bit stays
static void set_serves_plain_and_multi(void) {
    const char *name = "set e1 with a plain and a multiple wait";
    setup(EVENT_ATTR_MANUAL_RESET, EVENT_BIT_A, EVENT_WAIT_MODE_OR);
    const SceUID plain = start_plain();
    const SceUID multi = start_multi();
    const int set = sceKernelSetEvent(e1, EVENT_BIT_A, 0x11);
    const SceUInt32 waiters = waiters_of(e1);
    const SceUInt32 pattern_after_set = pattern_of(e1);
    test_join_thread(plain);
    test_join_thread(multi);
    const int pulse = sceKernelPulseEvent(e1, EVENT_BIT_A, 0x22);
    const SceUInt32 pattern_after_pulse = pattern_of(e1);
    PRINTF(set, "%s: the set", name);
    PRINTF(waiters, "%s: the waiters on e1 after the set", name);
    PRINTF(pattern_after_set, "%s: the pattern of e1 after the set", name);
    PRINTF(plain_result, "%s: the plain wait", name);
    PRINTF(plain_pattern, "%s: the plain wait's bits", name);
    PRINTF((int)plain_user_data, "%s: the plain wait's user data", name);
    PRINTF(multi_result, "%s: the multiple wait", name);
    print_entries(name, multi_result);
    PRINTF(pulse, "%s: the pulse of A", name);
    PRINTF(pattern_after_pulse, "%s: the pattern of e1 after the pulse", name);
    teardown();
}

// A multiple wait lists A or C on e1 and B on e2, and e1 is set with set_bits
static void entry_records_asked_bits(const char *name, SceUInt32 set_bits) {
    setup(EVENT_ATTR_MANUAL_RESET, EVENT_BIT_A | EVENT_BIT_C, EVENT_WAIT_MODE_OR);
    const SceUID multi = start_multi();
    const int set = sceKernelSetEvent(e1, set_bits, 0x55);
    const SceUInt32 pattern = pattern_of(e1);
    test_join_thread(multi);
    PRINTF(set, "%s: the set", name);
    PRINTF(multi_result, "%s: the multiple wait", name);
    print_entries(name, multi_result);
    PRINTF(pattern, "%s: the pattern of e1 after the wait", name);
    teardown();
}

// An auto reset e1 claims a multiple wait on its set before the wait runs, and a set of e2 passes
// the wait's e2 node by, so the e2 bit stays for a poll
static void claimed_multi_then_set(void) {
    const char *name = "auto reset, e1 claims the multiple wait, then e2 is set";
    setup(EVENT_ATTR_AUTO_RESET, EVENT_BIT_A, EVENT_WAIT_MODE_OR);
    const SceUID multi = start_multi();
    const int set_e1 = sceKernelSetEvent(e1, EVENT_BIT_A, 0x11);
    const SceUInt32 e1_waiters_1 = waiters_of(e1);
    const SceUInt32 e2_waiters_1 = waiters_of(e2);
    const int set_e2 = sceKernelSetEvent(e2, EVENT_BIT_B, 0x22);
    const SceUInt32 e1_waiters_2 = waiters_of(e1);
    const SceUInt32 e2_waiters_2 = waiters_of(e2);
    const SceUInt32 e2_pattern = pattern_of(e2);
    SceUInt32 poll_pattern = 0xDEAD;
    SceUInt64 poll_user_data = 0xDEAD;
    const int poll = sceKernelPollEvent(e2, EVENT_BIT_B, &poll_pattern, &poll_user_data);
    const SceUInt32 e2_pattern_after_poll = pattern_of(e2);
    test_join_thread(multi);
    PRINTF(set_e1, "%s: the set of e1", name);
    PRINTF(e1_waiters_1, "%s: the waiters on e1 after the set of e1", name);
    PRINTF(e2_waiters_1, "%s: the waiters on e2 after the set of e1", name);
    PRINTF(set_e2, "%s: the set of e2", name);
    PRINTF(e1_waiters_2, "%s: the waiters on e1 after the set of e2", name);
    PRINTF(e2_waiters_2, "%s: the waiters on e2 after the set of e2", name);
    PRINTF(e2_pattern, "%s: the pattern of e2 after the set of e2", name);
    PRINTF(poll, "%s: the poll of B on e2", name);
    PRINTF(poll_pattern, "%s: the poll's bits", name);
    PRINTF((int)poll_user_data, "%s: the poll's user data", name);
    PRINTF(e2_pattern_after_poll, "%s: the pattern of e2 after the poll", name);
    PRINTF(multi_result, "%s: the multiple wait", name);
    print_entries(name, multi_result);
    PRINTF(pattern_of(e1), "%s: the pattern of e1 at the end", name);
    teardown();
}

// A delete of e1 serves a multiple wait node that asked for the delete bit like a single waiter
static void delete_serves_delete_bit_node(void) {
    const char *name = "delete e1, a multiple wait lists 0x8 and A on it";
    setup(EVENT_ATTR_MANUAL_RESET, EVENT_DELETE | EVENT_BIT_A, EVENT_WAIT_MODE_OR);
    const SceUID plain = start_plain();
    const SceUID multi = start_multi();
    const int deleted = sceKernelDeleteSimpleEvent(e1);
    test_join_thread(plain);
    test_join_thread(multi);
    PRINTF(deleted, "%s: the delete", name);
    PRINTF(multi_result, "%s: the multiple wait", name);
    PRINTF(results[0].eventId == e1 ? 1 : 0, "%s: entry 0 is for e1", name);
    PRINTF(results[0].result, "%s: entry 0 result", name);
    PRINTF(results[0].resultPattern, "%s: entry 0 bits", name);
    PRINTF((SceUInt32)(results[0].userData >> 32) == (SceUInt32)sceKernelGetThreadId(),
           "%s: entry 0 user data high word is the deleting thread", name);
    PRINTF(plain_result, "%s: the plain wait", name);
    PRINTF(plain_pattern, "%s: the plain wait's bits", name);
    sceKernelDeleteSimpleEvent(e2);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    set_serves_plain_and_multi();
    entry_records_asked_bits("e1 listed with A or C, A set", EVENT_BIT_A);
    entry_records_asked_bits("e1 listed with A or C, A and D set", EVENT_BIT_A | EVENT_BIT_D);
    claimed_multi_then_set();
    delete_serves_delete_bit_node();

    test_exit();
    return 0;
}
