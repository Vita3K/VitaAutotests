// The event flag and simple event wait calls of the thread manager with a NULL or unmapped
// argument block, an unmapped timeout pointer in the block, and a NULL block for an id that isn't
// an object. Unmapped means the first page of the address space. Each wait would end at once, the
// flag and the event have the bits it asks for.
//
// On a PS Vita TV on firmware 3.65 each of these gives 0x80022005: the call copies its block in
// before it looks up the id, then the timeout, and only then waits.
#include "syscalls.h"
#include "test.h"

#define WAIT_OR 1
#define EVENT_BIT 0x01000000

typedef SceInt32 (*FlagWait)(SceUID, SceUInt32, SceUInt32, const WaitEventFlagBlock *);
typedef SceInt32 (*EventWait)(SceUID, SceUInt32, SceUInt32 *, const WaitEventBlock *);

static const void *const unmapped = (const void *)0x10;

static void flag_waits(const char *kind, FlagWait wait, SceUID flag) {
    SceUInt32 pattern = 0;
    const WaitEventFlagBlock bad_timeout = { &pattern, (SceUInt32 *)unmapped };
    PRINTF(wait(flag, 0x1, WAIT_OR, NULL), "%s with no block", kind);
    PRINTF(wait(flag, 0x1, WAIT_OR, unmapped), "%s with an unmapped block", kind);
    PRINTF(wait(flag, 0x1, WAIT_OR, &bad_timeout), "%s with an unmapped timeout", kind);
    PRINTF(wait(-1, 0x1, WAIT_OR, NULL), "%s of a bad id with no block", kind);
    const WaitEventFlagBlock good = { &pattern, NULL };
    PRINTF(wait(flag, 0x1, WAIT_OR, &good), "%s with a block", kind);
}

static void event_waits(const char *kind, EventWait wait, SceUID event) {
    SceUInt32 result = 0;
    SceUInt64 user_data = 0;
    const WaitEventBlock bad_timeout = { &user_data, (SceUInt32 *)unmapped };
    PRINTF(wait(event, EVENT_BIT, &result, NULL), "%s with no block", kind);
    PRINTF(wait(event, EVENT_BIT, &result, unmapped), "%s with an unmapped block", kind);
    PRINTF(wait(event, EVENT_BIT, &result, &bad_timeout), "%s with an unmapped timeout", kind);
    PRINTF(wait(-1, EVENT_BIT, &result, NULL), "%s of a bad id with no block", kind);
    const WaitEventBlock good = { &user_data, NULL };
    PRINTF(wait(event, EVENT_BIT, &result, &good), "%s with a block", kind);
}

int main(void) {
    const SceUID flag = sceKernelCreateEventFlag("flag", 0, 0x1, NULL);
    PRINT("create the event flag", test_created(flag));
    flag_waits("wait on the flag", syscall_wait_event_flag, flag);
    flag_waits("wait on the flag with callbacks", syscall_wait_event_flag_cb, flag);
    sceKernelDeleteEventFlag(flag);

    const SceUID event = sceKernelCreateSimpleEvent("event", 0, 0, NULL);
    PRINT("create the event", test_created(event));
    PRINT("set bit 24 of the event", sceKernelSetEvent(event, EVENT_BIT, 0));
    event_waits("wait on the event", syscall_wait_event, event);
    event_waits("wait on the event with callbacks", syscall_wait_event_cb, event);
    sceKernelDeleteSimpleEvent(event);

    test_exit();
    return 0;
}
