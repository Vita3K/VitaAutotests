// How setting, clearing, pulsing and waiting change the bits of simple events and event flags,
// and what bits a wait gets back
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID event, flag;
static SceUInt32 event_wait_pattern, event_result;
static SceUInt64 event_user_data;
static unsigned int flag_wait_pattern, flag_wait_mode, flag_bits;
static int wait_result;

// Waits on the simple event for event_wait_pattern
static int event_wait_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    wait_result = sceKernelWaitEvent(event, event_wait_pattern, &event_result, &event_user_data, &timeout);
    return 0;
}

// Waits on the event flag for flag_wait_pattern with flag_wait_mode
static int flag_wait_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    wait_result = sceKernelWaitEventFlag(flag, flag_wait_pattern, flag_wait_mode, &flag_bits, &timeout);
    return 0;
}

static SceUID start_event_waiter(SceUInt32 pattern) {
    event_wait_pattern = pattern;
    event_result = 0xDEAD;
    event_user_data = 0;
    wait_result = -1;
    const SceUID thread = test_start_thread(event_wait_thread);
    test_wait_until_waiting(thread);
    return thread;
}

static SceUID start_flag_waiter(unsigned int pattern, unsigned int mode) {
    flag_wait_pattern = pattern;
    flag_wait_mode = mode;
    flag_bits = 0xDEAD;
    wait_result = -1;
    const SceUID thread = test_start_thread(flag_wait_thread);
    test_wait_until_waiting(thread);
    return thread;
}

static SceUInt32 event_pattern(void) {
    SceUInt32 pattern = 0xDEAD;
    sceKernelGetEventPattern(event, &pattern);
    return pattern;
}

// Returns the bits of the event flag, without changing them
static unsigned int flag_pattern(void) {
    unsigned int bits = 0;
    if (sceKernelPollEventFlag(flag, 0xFFFFFFFF, EVF_WAITMODE_OR, &bits) < 0)
        return 0;
    return bits;
}

int main(void) {
    SceUInt32 result;
    SceUInt64 user_data;

    // Simple event, manual reset: set adds bits, and clear keeps the bits that are 1 in its pattern
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    sceKernelSetEvent(event, EVENT_BIT_A, 0);
    sceKernelSetEvent(event, EVENT_BIT_B, 0);
    PRINT("set adds bits", event_pattern());
    sceKernelClearEvent(event, EVENT_BIT_A);
    PRINT("clear keeps the bits of its pattern", event_pattern());
    sceKernelClearEvent(event, 0);
    PRINT("clear with 0 clears every bit", event_pattern());

    // A pulse clears the bits it pulses, even ones that were set, and leaves the others
    sceKernelSetEvent(event, EVENT_BIT_A | EVENT_BIT_B, 0);
    sceKernelPulseEvent(event, EVENT_BIT_A, 0);
    PRINT("pulse clears a bit that was set", event_pattern());
    sceKernelPulseEvent(event, EVENT_BIT_C, 0);
    PRINT("pulse doesn't leave a bit that wasn't set", event_pattern());
    SceUID waiter = start_event_waiter(EVENT_BIT_C);
    sceKernelPulseEvent(event, EVENT_BIT_C, 0x99);
    test_join_thread(waiter);
    PRINT("pulse wakes a waiter for its bit", wait_result);
    PRINT("the waiter gets every bit set at the pulse", event_result);
    PRINT("the waiter gets the pulse's user data", event_user_data == 0x99);
    PRINT("the other bits are left after the pulse", event_pattern());

    // Waits need any one of their bits, and a manual reset event keeps its bits
    PRINT("poll for one of two bits", sceKernelPollEvent(event, EVENT_BIT_B | EVENT_BIT_C, &result, &user_data));
    PRINT("the poll gets every bit", result);
    PRINT("a manual reset poll keeps the bits", event_pattern());

    // Deleting the event ends a wait for a user bit, and reports the delete event to a wait for it
    waiter = start_event_waiter(EVENT_BIT_D);
    sceKernelDeleteSimpleEvent(event);
    test_join_thread(waiter);
    PRINT("a wait for a user bit on a deleted event", wait_result);
    PRINT("its result", event_result);
    event = sceKernelCreateSimpleEvent("deleted", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    waiter = start_event_waiter(EVENT_DELETE);
    sceKernelDeleteSimpleEvent(event);
    test_join_thread(waiter);
    PRINT("a wait for the delete event on a deleted event", wait_result);
    PRINT("its result", event_result);

    // Simple event, auto reset: a take clears the bits it waited for
    event = sceKernelCreateSimpleEvent("auto", EVENT_ATTR_AUTO_RESET, 0, NULL);
    sceKernelSetEvent(event, EVENT_BIT_A | EVENT_BIT_B, 0);
    PRINT("poll an auto reset event", sceKernelPollEvent(event, EVENT_BIT_A, &result, &user_data));
    PRINT("the poll gets every bit", result);
    PRINT("the poll clears the bit it waited for", event_pattern());
    PRINT("poll the other bit", sceKernelPollEvent(event, EVENT_BIT_B | EVENT_BIT_C, &result, &user_data));
    PRINT("that clears it too", event_pattern());
    sceKernelPulseEvent(event, EVENT_BIT_A, 0);
    PRINT("a pulse with no waiter leaves nothing", event_pattern());

    // Event flag: set adds bits, and clear keeps the bits that are 1 in its pattern
    flag = sceKernelCreateEventFlag("flag", 0, 0x1, NULL);
    sceKernelSetEventFlag(flag, 0x2);
    PRINT("set adds bits to the flag", flag_pattern());
    sceKernelClearEventFlag(flag, 0x1);
    PRINT("clear keeps the bits of its pattern", flag_pattern());
    sceKernelClearEventFlag(flag, 0);

    // An AND wait needs every bit, even when they come from separate sets
    waiter = start_flag_waiter(0x3, EVF_WAITMODE_AND);
    sceKernelSetEventFlag(flag, 0x1);
    test_let_threads_wait();
    PRINT("an AND wait goes on with one of its bits", wait_result == -1);
    sceKernelSetEventFlag(flag, 0x2);
    test_join_thread(waiter);
    PRINT("it ends when the other one is set", wait_result);
    PRINT("and gets both", flag_bits);

    // An OR wait ends with any of its bits
    waiter = start_flag_waiter(0xC, EVF_WAITMODE_OR);
    sceKernelSetEventFlag(flag, 0x4);
    test_join_thread(waiter);
    PRINT("an OR wait ends with one of its bits", wait_result);
    PRINT("and gets every bit of the flag", flag_bits);

    // Waits that clear: the result is from before the clear
    waiter = start_flag_waiter(0x1, EVF_WAITMODE_AND | EVF_WAITMODE_CLEAR_PAT);
    test_join_thread(waiter);
    PRINT("a wait that clears its pattern", wait_result);
    PRINT("gets the bits from before clearing", flag_bits);
    PRINT("and clears only its pattern", flag_pattern());
    waiter = start_flag_waiter(0x2, EVF_WAITMODE_OR | EVF_WAITMODE_CLEAR_ALL);
    test_join_thread(waiter);
    PRINT("a wait that clears every bit", wait_result);
    PRINT("gets the bits from before clearing", flag_bits);
    PRINT("and clears them all", flag_pattern());
    waiter = start_flag_waiter(0x8, EVF_WAITMODE_AND | EVF_WAITMODE_CLEAR_PAT);
    sceKernelSetEventFlag(flag, 0x9);
    test_join_thread(waiter);
    PRINT("a set ends a wait that clears its pattern", wait_result);
    PRINT("which gets the bits that were set", flag_bits);
    PRINT("and clears only its pattern after the set", flag_pattern());

    test_exit();
    return 0;
}
