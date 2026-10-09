// Names, info sizes, UID classes, timers and callbacks in less common cases
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// 31 characters, the longest name
#define NAME_31 "abcdefghijklmnopqrstuvwxyz01234"

static int calls;

static int delete_me(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    calls++;
    return 1;
}

int main(void) {
    // Names
    PRINT("a NULL name", test_created(sceKernelCreateSema(NULL, 0, 0, 1, NULL)));
    PRINT("an empty name", test_created(sceKernelCreateSema("", 0, 0, 1, NULL)));
    PRINT("a 31 character name on an openable object", test_created(sceKernelCreateSema(NAME_31, ATTR_OPENABLE, 0, 1, NULL)));
    PRINT("two openable objects with the same name", test_created(sceKernelCreateSema(NAME_31, ATTR_OPENABLE, 0, 1, NULL)));

    // Info sizes
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 4, NULL);
    SceKernelSemaInfo info;
    sceClibMemset(&info, 0, sizeof(info));
    info.size = 0;
    PRINT("info with a size of 0", sceKernelGetSemaInfo(sema, &info));
    sceClibMemset(&info, 0, sizeof(info));
    info.size = 8;
    PRINT("info with a size of 8", sceKernelGetSemaInfo(sema, &info));
    PRINT("the UID with a size of 8", info.semaId == sema);
    PRINT("the maximum with a size of 8", info.maxCount);
    sceClibMemset(&info, 0, sizeof(info));
    info.size = sizeof(info) + 4;
    PRINT("info with a size past the struct", sceKernelGetSemaInfo(sema, &info));
    PRINT("info without a pointer", sceKernelGetSemaInfo(sema, NULL));

    // UID classes of UIDs that aren't objects
    PRINT("the class of UID 0", get_uid_class(0));
    PRINT("the class of UID -1", get_uid_class(-1));
    PRINT("the class of an unknown UID", get_uid_class(0x12345));
    PRINT("the class of the process", get_uid_class(sceKernelGetProcessId()));

    // Timers
    const SceUID timer = sceKernelCreateTimer("timer", 0, NULL);
    SceUInt64 interval = 0;
    PRINT("set an event with an interval of 0", sceKernelSetTimerEvent(timer, 0, &interval, 0));
    PRINT("set an event of type 2", sceKernelSetTimerEvent(timer, 2, &interval, 0));
    PRINT("set an event without an interval", sceKernelSetTimerEvent(timer, 0, NULL, 0));
    PRINT("the time of an unknown timer", (int)sceKernelGetTimerTimeWide(0x12345));
    PRINT("start an unknown timer", sceKernelStartTimer(0x12345));

    // A callback that returns non-zero
    const SceUID cb = sceKernelCreateCallback("callback", 0, delete_me, NULL);
    sceKernelNotifyCallback(cb, 1);
    PRINT("check runs it", sceKernelCheckCallback());
    PRINT("it ran", calls);
    PRINT("its count after returning non-zero", sceKernelGetCallbackCount(cb));
    PRINT("notify it after returning non-zero", sceKernelNotifyCallback(cb, 2));
    PRINT("check with nothing pending", sceKernelCheckCallback());

    test_exit();
    return 0;
}
