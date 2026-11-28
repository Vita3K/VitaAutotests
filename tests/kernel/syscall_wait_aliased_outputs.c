// The event flag and simple event wait syscalls with an output pointer that points into the
// argument block itself: the flag's result pattern written over the block's result pattern field,
// and the event's result pattern written over the block's user data field. The waits end at once.
//
// On a PS Vita TV on firmware 3.65 the waits give 0: the call copies the block in before it waits,
// so it writes its outputs where the block pointed before the wait.
#include <psp2/kernel/threadmgr.h>

#include "syscalls.h"
#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    const SceUID flag = sceKernelCreateEventFlag("flag", 0, 0x5, NULL);
    PRINT("create the event flag", test_created(flag));
    SceUInt32 pattern = 0xDEAD;
    WaitEventFlagBlock flag_block = { &pattern, NULL };
    flag_block.result_pattern = (SceUInt32 *)&flag_block.result_pattern;
    PRINT("wait with the result pattern on the block's own field", syscall_wait_event_flag(flag, 0x1, SCE_EVENT_WAITOR, &flag_block));
    PRINT("the field holds the pattern", (SceUInt32)(uintptr_t)flag_block.result_pattern);
    sceKernelDeleteEventFlag(flag);

    const SceUID event = sceKernelCreateSimpleEvent("event", 0, 0, NULL);
    PRINT("create the event", test_created(event));
    PRINT("set the event", sceKernelSetEvent(event, EVENT_BIT_A, 0x1234));
    SceUInt64 user_data = 0;
    WaitEventBlock event_block = { &user_data, NULL };
    PRINT("wait with the result pattern on the block's user data field", syscall_wait_event(event, EVENT_BIT_A, (SceUInt32 *)&event_block.user_data, &event_block));
    PRINT("the field holds the pattern", (SceUInt32)(uintptr_t)event_block.user_data);
    PRINT("the user data went where the field pointed", (SceUInt32)user_data);
    sceKernelDeleteSimpleEvent(event);

    test_exit();
    return 0;
}
