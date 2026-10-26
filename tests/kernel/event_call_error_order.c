// Which error an event flag or simple event call reports when more than one of its arguments, or
// its state, is wrong at once. This is a probe: every call is made with its out values preset to
// 0xDEAD, and each result and out value is printed as it comes, so the output is whatever order the
// implementation checks in. The main thread runs at 0x60, and the holder thread of the middle part
// at 0xB0, both pinned to core 0. The cases:
// - an event flag with no thread holding it: a wait, a wait with callbacks and a poll, each with a
//   bad mode 0x8 and pattern 0, with both clear modes and pattern 0, with a bad mode and an unknown
//   UID, and with pattern 0 and an unknown UID
// - an event flag held by another thread, which waits on it without the multi attribute: a wait,
//   a wait with callbacks and a poll, each with a bad mode 0x8 and pattern 0x1, then with pattern 0
// - a simple event: a poll and a wait with pattern 0 and an unknown UID, then a set, a pulse, a
//   clear and a cancel with a set pattern, first with an unknown UID and bit 0x1, then with the
//   event and bit 0x1, then with the event and bits 0x1 and A. After each group the pattern left on
//   the event is printed.
// - creation, with an option struct whose size field is 8: an event flag with a bad attribute, with
//   a bad attribute and init pattern 0xFFFFFFFF, a simple event with a bad attribute, and a simple
//   event with init pattern 0xFF. Last, an event flag with a NULL name and a bad attribute.
// Objects the creation cases make are deleted at the end.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define UNKNOWN_UID 0x12345

// An option struct whose size field is 8, which the creation calls get as their option
typedef struct OptParam8 {
    SceSize size;
    SceUInt32 reserved;
} OptParam8;

static SceUID held;
static volatile int holder_result = NOT_DONE;

// Waits for bit 0x8 of the held flag, which makes this thread its holder until it ends
static int holder(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    unsigned int bits = NOT_DONE;
    SceUInt timeout = 3 * TEST_WAIT_US;
    holder_result = sceKernelWaitEventFlag(held, 0x8, SCE_EVENT_WAITOR, &bits, &timeout);
    return 0;
}

// Makes a wait, a wait with callbacks and a poll of flag for pattern and mode, each with its out
// bits preset, and prints each result and out bits
static void flag_calls(const char *name, SceUID flag, unsigned int pattern, unsigned int mode) {
    unsigned int bits = NOT_DONE;
    SceUInt timeout = 3 * TEST_WAIT_US;
    PRINTF(sceKernelWaitEventFlag(flag, pattern, mode, &bits, &timeout), "%s: wait", name);
    PRINTF(bits, "%s: wait's out bits", name);
    bits = NOT_DONE;
    timeout = 3 * TEST_WAIT_US;
    PRINTF(sceKernelWaitEventFlagCB(flag, pattern, mode, &bits, &timeout), "%s: wait with callbacks", name);
    PRINTF(bits, "%s: wait with callbacks' out bits", name);
    bits = NOT_DONE;
    PRINTF(sceKernelPollEventFlag(flag, pattern, mode, &bits), "%s: poll", name);
    PRINTF(bits, "%s: poll's out bits", name);
}

// Makes a poll and then a wait for pattern on simple event id, each with its out values preset, and
// prints each result and out value
static void simple_poll_and_wait(const char *name, SceUID id, SceUInt32 pattern) {
    SceUInt32 bits = NOT_DONE;
    SceUInt64 user_data = NOT_DONE;
    PRINTF(sceKernelPollEvent(id, pattern, &bits, &user_data), "%s: poll", name);
    PRINTF(bits, "%s: poll's out bits", name);
    PRINTF((SceUInt32)user_data, "%s: poll's out user data", name);
    bits = NOT_DONE;
    user_data = NOT_DONE;
    SceUInt32 timeout = 3 * TEST_WAIT_US;
    PRINTF(sceKernelWaitEvent(id, pattern, &bits, &user_data, &timeout), "%s: wait", name);
    PRINTF(bits, "%s: wait's out bits", name);
    PRINTF((SceUInt32)user_data, "%s: wait's out user data", name);
}

// Makes a set, a pulse, a clear and a cancel with a set pattern on simple event id, each with bits,
// and prints each result, the waiters the cancel counted, and the pattern left on the event
static void simple_calls(const char *name, SceUID id, SceUInt32 bits) {
    PRINTF(sceKernelSetEvent(id, bits, 0), "%s: set", name);
    PRINTF(sceKernelPulseEvent(id, bits, 0), "%s: pulse", name);
    PRINTF(sceKernelClearEvent(id, bits), "%s: clear", name);
    SceUInt32 waiting = NOT_DONE;
    PRINTF(sceKernelCancelEventWithSetPattern(id, bits, 0, &waiting), "%s: cancel with a set pattern", name);
    PRINTF(waiting, "%s: the waiters the cancel counted", name);
    SceUInt32 pattern = NOT_DONE;
    PRINTF(sceKernelGetEventPattern(id, &pattern), "%s: the pattern left, its result", name);
    PRINTF(pattern, "%s: the pattern left", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    // Event flags with no thread holding them
    const SceUID flag = sceKernelCreateEventFlag("flag", 0, 0x3, NULL);
    flag_calls("flag, bad mode 0x8 and pattern 0", flag, 0, 0x8);
    flag_calls("flag, both clear modes and pattern 0", flag, 0,
        SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR | SCE_EVENT_WAITCLEAR_PAT);
    flag_calls("flag, bad mode 0x8 and an unknown UID", UNKNOWN_UID, 0x1, 0x8);
    flag_calls("flag, pattern 0 and an unknown UID", UNKNOWN_UID, 0, SCE_EVENT_WAITOR);

    // Another thread holds the flag, since it waits on it without the multi attribute
    held = sceKernelCreateEventFlag("held", 0, 0, NULL);
    const SceUID holder_thread = sceKernelCreateThread(
        "holder", holder, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(holder_thread, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(holder_thread, WAIT_TYPE_EVENT_FLAG, held));
    flag_calls("held by another thread, bad mode 0x8 and pattern 0x1", held, 0x1, 0x8);
    flag_calls("held by another thread, pattern 0", held, 0, SCE_EVENT_WAITOR);
    PRINT("the release of the hold, a set of 0x8", sceKernelSetEventFlag(held, 0x8));
    test_join_thread(holder_thread);
    PRINT("the holder's result", holder_result);
    sceKernelDeleteEventFlag(held);

    // Simple events
    const SceUID simple = sceKernelCreateSimpleEvent("event", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    simple_poll_and_wait("simple event, pattern 0 and an unknown UID", UNKNOWN_UID, 0);
    simple_calls("simple event, an unknown UID and bit 0x1", UNKNOWN_UID, 0x1);
    simple_calls("simple event, the event and bit 0x1", simple, 0x1);
    simple_calls("simple event, the event and bits 0x1 and A", simple, 0x1 | EVENT_BIT_A);

    // Creation with an option struct whose size field is 8, which the calls refuse
    OptParam8 opt = { .size = 8 };
    const SceUID flag_bad_attr =
        sceKernelCreateEventFlag("flag", 0x80000000, 0, (SceKernelEventFlagOptParam *)&opt);
    PRINT("event flag, a bad attribute and an option of size 8", test_created(flag_bad_attr));
    const SceUID flag_pattern =
        sceKernelCreateEventFlag("flag", 0, 0xFFFFFFFF, (SceKernelEventFlagOptParam *)&opt);
    PRINT("event flag, an option of size 8 and init pattern 0xFFFFFFFF", test_created(flag_pattern));
    const SceUID simple_bad_attr = sceKernelCreateSimpleEvent("event", 0x80000000, 0, &opt);
    PRINT("simple event, a bad attribute and an option of size 8", test_created(simple_bad_attr));
    const SceUID simple_pattern = sceKernelCreateSimpleEvent("event", 0, 0xFF, &opt);
    PRINT("simple event, an option of size 8 and init pattern 0xFF", test_created(simple_pattern));

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    const SceUID flag_null_name = sceKernelCreateEventFlag(NULL, 0x80000000, 0, NULL);
    PRINT("event flag, a NULL name and a bad attribute", test_created(flag_null_name));

    if (flag_bad_attr >= 0)
        sceKernelDeleteEventFlag(flag_bad_attr);
    if (flag_pattern >= 0)
        sceKernelDeleteEventFlag(flag_pattern);
    if (simple_bad_attr >= 0)
        sceKernelDeleteSimpleEvent(simple_bad_attr);
    if (simple_pattern >= 0)
        sceKernelDeleteSimpleEvent(simple_pattern);
    if (flag_null_name >= 0)
        sceKernelDeleteEventFlag(flag_null_name);
    sceKernelDeleteEventFlag(flag);
    sceKernelDeleteSimpleEvent(simple);

    test_exit();
    return 0;
}
