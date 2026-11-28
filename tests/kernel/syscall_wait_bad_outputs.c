// The event flag and simple event wait syscalls with an output pointer that isn't mapped, the first
// page of the address space: the flag's result pattern, the event's result pattern and its user
// data. The waits can end at once. Each wait clears what it waited for, so a later look shows
// whether the wait ran.
//
// On a PS Vita TV on firmware 3.65 the wait runs and then gives 0x80022005, as the call writes its
// outputs back after the wait, also when the id is bad.
#include <psp2/kernel/threadmgr.h>

#include "syscalls.h"
#include "test.h"
#include "threadmgr_extra.h"

static void *const unmapped = (void *)0x10;

int main(void) {
    SceUInt32 pattern = 0, timeout = 0;
    SceKernelEventFlagInfo info;
    info.size = sizeof(info);

    const SceUID flag = sceKernelCreateEventFlag("flag", 0, 0x3, NULL);
    PRINT("create the event flag", test_created(flag));
    const WaitEventFlagBlock bad_pattern = { (SceUInt32 *)unmapped, NULL };
    PRINT("wait on bit 0 with an unmapped result pattern", syscall_wait_event_flag(flag, 0x1, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR, &bad_pattern));
    sceKernelGetEventFlagInfo(flag, &info);
    PRINT("the flag's pattern after it", info.currentPattern);
    PRINT("set bit 1", sceKernelSetEventFlag(flag, 0x2));
    PRINT("wait on bit 1 with callbacks and an unmapped result pattern", syscall_wait_event_flag_cb(flag, 0x2, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR, &bad_pattern));
    sceKernelGetEventFlagInfo(flag, &info);
    PRINT("the flag's pattern after it", info.currentPattern);
    PRINT("wait on a bad id with an unmapped result pattern", syscall_wait_event_flag(-1, 0x1, SCE_EVENT_WAITOR, &bad_pattern));
    sceKernelDeleteEventFlag(flag);

    const SceUID event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL);
    PRINT("create the event", test_created(event));
    SceUInt64 user_data = 0;
    const WaitEventBlock good = { &user_data, NULL };
    const WaitEventBlock bad_user_data = { (SceUInt64 *)unmapped, NULL };
    const WaitEventBlock poll = { &user_data, &timeout };

    PRINT("set the event", sceKernelSetEvent(event, EVENT_BIT_A, 0));
    PRINT("wait with an unmapped result pattern", syscall_wait_event(event, EVENT_BIT_A, (SceUInt32 *)unmapped, &good));
    timeout = 0;
    PRINT("poll the event after it", syscall_wait_event(event, EVENT_BIT_A, &pattern, &poll));

    PRINT("set the event again", sceKernelSetEvent(event, EVENT_BIT_A, 0));
    PRINT("wait with callbacks and an unmapped user data", syscall_wait_event_cb(event, EVENT_BIT_A, &pattern, &bad_user_data));
    timeout = 0;
    PRINT("poll the event after it", syscall_wait_event(event, EVENT_BIT_A, &pattern, &poll));

    PRINT("wait on a bad id with an unmapped result pattern", syscall_wait_event(-1, EVENT_BIT_A, (SceUInt32 *)unmapped, &good));
    PRINT("wait on a bad id with an unmapped user data", syscall_wait_event(-1, EVENT_BIT_A, &pattern, &bad_user_data));
    sceKernelDeleteSimpleEvent(event);

    test_exit();
    return 0;
}
