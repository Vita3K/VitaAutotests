// The event flag and simple event wait calls of the thread manager, called with the argument block
// libkernel builds: the result pattern or user data pointer and the timeout pointer in one block
// passed as the fourth argument.
#include "syscalls.h"
#include "test.h"

#define WAIT_AND 0
#define WAIT_OR 1
#define TIMEOUT_US 200000

// Prints a wait's result, the pattern it wrote and whether it wrote the time left
static void print_flag_wait(const char *what, SceInt32 result, SceUInt32 pattern, SceUInt32 timeout) {
    PRINTF(result, "%s", what);
    PRINTF(pattern, "%s: the result pattern", what);
    PRINTF(timeout == 0, "%s: the time left is 0", what);
    PRINTF(timeout > 0 && timeout <= TIMEOUT_US, "%s: the time left is between 0 and the timeout", what);
}

static void flag_waits(const char *kind, SceInt32 (*wait)(SceUID, SceUInt32, SceUInt32, const WaitEventFlagBlock *)) {
    char what[64];
    const SceUID flag = sceKernelCreateEventFlag("flag", 0, 0x3, NULL);

    SceUInt32 pattern = 0xDEAD, timeout = TIMEOUT_US;
    WaitEventFlagBlock block = { &pattern, &timeout };
    sceClibSnprintf(what, sizeof(what), "%s, bits that are set", kind);
    SceInt32 result = wait(flag, 0x1, WAIT_AND, &block);
    print_flag_wait(what, result, pattern, timeout);

    pattern = 0xDEAD;
    timeout = TIMEOUT_US;
    sceClibSnprintf(what, sizeof(what), "%s, a bit that is never set", kind);
    result = wait(flag, 0x4, WAIT_OR, &block);
    print_flag_wait(what, result, pattern, timeout);

    // A block with no pointers waits forever and writes nothing
    WaitEventFlagBlock empty = { NULL, NULL };
    PRINTF(wait(flag, 0x2, WAIT_AND, &empty), "%s, no result or timeout pointer", kind);

    sceKernelDeleteEventFlag(flag);
}

static void event_waits(const char *kind, SceInt32 (*wait)(SceUID, SceUInt32, SceUInt32 *, const WaitEventBlock *)) {
    const SceUID event = sceKernelCreateSimpleEvent("event", 0, 0, NULL);
    sceKernelSetEvent(event, EVENT_BIT_A, 0x0000123400005678ULL);

    SceUInt32 result = 0xDEAD, timeout = TIMEOUT_US;
    SceUInt64 user_data = 0xDEADDEADDEADDEADULL;
    WaitEventBlock block = { &user_data, &timeout };
    PRINTF(wait(event, EVENT_BIT_A, &result, &block), "%s, a bit that is set", kind);
    PRINTF(result, "%s, a bit that is set: the result pattern", kind);
    PRINTF((SceUInt32)user_data, "%s, a bit that is set: the user data low word", kind);
    PRINTF((SceUInt32)(user_data >> 32), "%s, a bit that is set: the user data high word", kind);
    PRINTF(timeout > 0 && timeout <= TIMEOUT_US, "%s, a bit that is set: the time left is between 0 and the timeout", kind);

    timeout = TIMEOUT_US;
    PRINTF(wait(event, EVENT_BIT_B, &result, &block), "%s, a bit that is never set", kind);
    PRINTF(timeout, "%s, a bit that is never set: the time left", kind);

    sceKernelDeleteSimpleEvent(event);
}

int main(void) {
    flag_waits("event flag", syscall_wait_event_flag);
    flag_waits("event flag with callbacks", syscall_wait_event_flag_cb);
    event_waits("simple event", syscall_wait_event);
    event_waits("simple event with callbacks", syscall_wait_event_cb);

    test_exit();
    return 0;
}
