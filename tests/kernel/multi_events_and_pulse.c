// What an AND sceKernelWaitMultipleEvents takes from each event when the event is pulsed or set,
// and what it keeps when it ends by a timeout. The events are simple events a and b, which wait
// for EVENT_BIT_A and EVENT_BIT_B, and the thread waits on both. Every thread runs on core 0, the
// waiter at 0xB0 below the main thread at 0x60. The main thread reads what it needs before it
// joins the waiter, and prints after the join. Cases:
// - manual reset: a is pulsed with user data 0x11 while the wait goes on. The wait is still there
//   and a's user bits are clear. Then b is set with 0x22, which ends the wait with an entry for
//   each event.
// - manual reset: a is set, then cleared with ClearEvent, then b is set. The entry for a keeps the
//   bit and the user data it had at the set, and a's pattern is clear.
// - auto reset: a is set and b never is, and the wait times out after 1 s. Entry 0 holds a's bit,
//   and the bit is not given back at the timeout. A later set and poll of a work as usual.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define MULTI_EVENTS_WAIT_ID ((SceUID)0x80000000)

// Each wait gets three times the test's wait limit, so a wait that stays queued still ends
#define WAIT_LIMIT (3 * TEST_WAIT_US)

// The timeout of the case that times out, and the spin that runs past it
#define SHORT_TIMEOUT 1000000
#define SPIN_PAST_TIMEOUT 1600000

// The user bits of an event's pattern, without the system bits
#define USER_BITS 0xFF000000

static SceUID a, b;
static WaitEvent list[2];
static ResultEvent results[2];
static SceUInt32 wait_timeout;
static volatile int wait_result;
static volatile int wait_returned;

// Waits for both events with AND
static int and_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = wait_timeout;
    wait_result = sceKernelWaitMultipleEvents(list, 2, EVENT_WAIT_MODE_AND, results, &timeout);
    wait_returned = 1;
    return 0;
}

// Starts the waiter on core 0, and returns once it is in its wait or has returned
static SceUID start_waiter(void) {
    const SceUID thread = sceKernelCreateThread("and", and_waiter, WAITER_PRIORITY, 0x4000, 0,
                                                SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_MP_EVENTS, MULTI_EVENTS_WAIT_ID) ||
                  wait_returned);
    return thread;
}

// Creates a and b with attr, and sets the timeout of the wait
static void setup(SceUInt32 attr, SceUInt32 timeout) {
    a = sceKernelCreateSimpleEvent("a", attr, 0, NULL);
    b = sceKernelCreateSimpleEvent("b", attr, 0, NULL);
    list[0] = (WaitEvent){ a, EVENT_BIT_A };
    list[1] = (WaitEvent){ b, EVENT_BIT_B };
    wait_timeout = timeout;
    wait_result = 0xDEAD;
    wait_returned = 0;
    sceClibMemset(results, 0xAA, sizeof(results));
}

static void teardown(void) {
    sceKernelDeleteSimpleEvent(a);
    sceKernelDeleteSimpleEvent(b);
}

// Returns the user bits of the event's pattern, or 0xDEAD if it can't be read
static SceUInt32 user_bits_of(SceUID event) {
    SceUInt32 pattern = 0xDEAD;
    if (sceKernelGetEventPattern(event, &pattern) < 0)
        return 0xDEAD;
    return pattern & USER_BITS;
}

// Returns whether thread is still in the multiple wait
static int still_waiting(SceUID thread) {
    return test_in_wait(thread, WAIT_TYPE_MP_EVENTS, MULTI_EVENTS_WAIT_ID);
}

// Prints the first count entries the wait filled, each as the event it is for
static void print_entries(const char *name, int count) {
    for (int i = 0; i < 2 && i < count; i++) {
        PRINTF(results[i].eventId == a ? 1 : results[i].eventId == b ? 2 : 0,
               "%s: entry %d is for event 1 or 2", name, i);
        PRINTF(results[i].result, "%s: entry %d result", name, i);
        PRINTF(results[i].resultPattern, "%s: entry %d bits", name, i);
        PRINTF((int)results[i].userData, "%s: entry %d user data", name, i);
    }
}

// A pulse of a is taken by the wait, and the bit is clear again after the pulse
static void pulse_then_set(void) {
    const char *name = "manual reset, pulse a, then set b";
    setup(EVENT_ATTR_MANUAL_RESET, WAIT_LIMIT);
    const SceUID thread = start_waiter();
    const int pulse = sceKernelPulseEvent(a, EVENT_BIT_A, 0x11);
    const int waits_after_pulse = still_waiting(thread);
    const SceUInt32 a_after_pulse = user_bits_of(a);
    const int set = sceKernelSetEvent(b, EVENT_BIT_B, 0x22);
    test_join_thread(thread);
    PRINTF(pulse, "%s: the pulse of a", name);
    PRINTF(waits_after_pulse, "%s: still waiting after the pulse", name);
    PRINTF(a_after_pulse, "%s: a's user bits after the pulse", name);
    PRINTF(set, "%s: the set of b", name);
    PRINTF(wait_result, "%s: the wait", name);
    print_entries(name, wait_result);
    PRINTF(user_bits_of(a), "%s: a's user bits at the end", name);
    PRINTF(user_bits_of(b), "%s: b's user bits at the end", name);
    teardown();
}

// A ClearEvent of a after its set, while the wait goes on, leaves the entry for a as it is
static void set_clear_then_set(void) {
    const char *name = "manual reset, set a, clear a, then set b";
    setup(EVENT_ATTR_MANUAL_RESET, WAIT_LIMIT);
    const SceUID thread = start_waiter();
    const int set_a = sceKernelSetEvent(a, EVENT_BIT_A, 0x11);
    const int waits_after_set = still_waiting(thread);
    const int clear = sceKernelClearEvent(a, 0);
    const SceUInt32 a_after_clear = user_bits_of(a);
    const int waits_after_clear = still_waiting(thread);
    const int set_b = sceKernelSetEvent(b, EVENT_BIT_B, 0x22);
    test_join_thread(thread);
    PRINTF(set_a, "%s: the set of a", name);
    PRINTF(waits_after_set, "%s: still waiting after the set of a", name);
    PRINTF(clear, "%s: the clear of a", name);
    PRINTF(a_after_clear, "%s: a's user bits after the clear", name);
    PRINTF(waits_after_clear, "%s: still waiting after the clear", name);
    PRINTF(set_b, "%s: the set of b", name);
    PRINTF(wait_result, "%s: the wait", name);
    print_entries(name, wait_result);
    PRINTF(user_bits_of(a), "%s: a's user bits at the end", name);
    PRINTF(user_bits_of(b), "%s: b's user bits at the end", name);
    teardown();
}

// An auto reset a taken by a wait that times out before b is set keeps the bit taken, and a later
// set and poll of a take the bit as usual
static void timeout_keeps_taken_bit(void) {
    const char *name = "auto reset, a set, b never set, the wait times out";
    setup(EVENT_ATTR_AUTO_RESET, SHORT_TIMEOUT);
    const SceUID thread = start_waiter();
    const int set_a = sceKernelSetEvent(a, EVENT_BIT_A, 0x11);
    const int waits_after_set = still_waiting(thread);
    const SceUInt32 a_after_set = user_bits_of(a);
    // Spins past the timeout of the wait, which then ends before its bits are read
    sceKernelDelayThread(SPIN_PAST_TIMEOUT);
    const SceUInt32 a_after_timeout = user_bits_of(a);
    test_join_thread(thread);
    const int set_again = sceKernelSetEvent(a, EVENT_BIT_A, 0x33);
    SceUInt32 poll_pattern = 0xDEAD;
    SceUInt64 poll_user_data = 0xDEAD;
    const int poll = sceKernelPollEvent(a, EVENT_BIT_A, &poll_pattern, &poll_user_data);
    const SceUInt32 a_after_poll = user_bits_of(a);
    PRINTF(set_a, "%s: the set of a", name);
    PRINTF(waits_after_set, "%s: still waiting after the set of a", name);
    PRINTF(a_after_set, "%s: a's user bits after the set", name);
    PRINTF(wait_result, "%s: the wait", name);
    print_entries(name, 1);
    PRINTF(a_after_timeout, "%s: a's user bits after the timeout", name);
    PRINTF(set_again, "%s: a later set of a", name);
    PRINTF(poll, "%s: a later poll of a", name);
    PRINTF(poll_pattern, "%s: the poll's bits", name);
    PRINTF((int)poll_user_data, "%s: the poll's user data", name);
    PRINTF(a_after_poll, "%s: a's user bits after the poll", name);
    teardown();
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    pulse_then_set();
    set_clear_then_set();
    // Last, since it spins past a timeout
    timeout_keeps_taken_bit();

    test_exit();
    return 0;
}
