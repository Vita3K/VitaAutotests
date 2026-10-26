// What the out values of a failed call hold afterwards. Each out value is
// preset to 0xBEEF, so a value a call wrote shows apart from one it left alone.
// The cases:
// - sceKernelWaitEventFlag on a valid flag with mode 0x8, which the call rejects, and on an unknown
//   UID, each printing its result and the out bits
// - sceKernelCancelEvent and sceKernelCancelTimer on an unknown UID, each printing its result and the
//   count it wrote
// - the user data of a timer right after it was created, which is compared with the creating
//   thread's UID in the high word and the process ID in the low word, and the user data after the
//   timer's first event fired, which is compared with 0
// cancel_unknown_counts covers the cancels on unknown UIDs with the counts preset to 0xDEAD, and
// event_flag_errors covers the event flag calls on bad UIDs, so the presets are what is new here.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define PRESET 0xBEEF
#define UNKNOWN_UID 0x12345

int main(void) {
    SceUID flag = sceKernelCreateEventFlag("flag", 0, 0x1, NULL);
    SceUInt32 bits = PRESET;
    SceUInt timeout = 1000;
    PRINT("wait with mode 0x8 on a flag",
        sceKernelWaitEventFlag(flag, 0x1, 0x8, &bits, &timeout));
    PRINT("wait with mode 0x8 on a flag: the out bits", bits);
    bits = PRESET;
    timeout = 1000;
    PRINT("wait on an unknown UID",
        sceKernelWaitEventFlag(UNKNOWN_UID, 0x1, SCE_EVENT_WAITOR, &bits,
            &timeout));
    PRINT("wait on an unknown UID: the out bits", bits);
    sceKernelDeleteEventFlag(flag);

    SceUInt32 count = PRESET;
    PRINT("cancel of an event on an unknown UID",
        sceKernelCancelEvent(UNKNOWN_UID, &count));
    PRINT("cancel of an event on an unknown UID: the count", count);
    count = PRESET;
    PRINT("cancel of a timer on an unknown UID",
        sceKernelCancelTimer(UNKNOWN_UID, &count));
    PRINT("cancel of a timer on an unknown UID: the count", count);

    // The user data a new timer reports is the creating thread and the process
    SceUID timer = sceKernelCreateTimer("timer", 0, NULL);
    EventInfo info = { .size = sizeof(info) };
    sceKernelGetEventInfo(timer, &info);
    const SceUInt64 creator = ((SceUInt64)(SceUInt32)sceKernelGetThreadId() << 32) | (SceUInt32)sceKernelGetProcessId();
    PRINT("timer user data after create is the thread and process",
        info.userData == creator);

    // Its first event fires once, and the user data after that
    SceUInt64 interval = 100000;
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    sceKernelStartTimer(timer);
    SceUInt32 event_bits = 0;
    SceUInt64 event_user_data = 0;
    timeout = TEST_WAIT_US;
    PRINT("the first event fired",
        sceKernelWaitEvent(timer, EVENT_TIMER, &event_bits, &event_user_data,
            &timeout));
    EventInfo fired = { .size = sizeof(fired) };
    sceKernelGetEventInfo(timer, &fired);
    PRINT("timer user data after its first event is 0", fired.userData == 0);

    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
    test_exit();
    return 0;
}
