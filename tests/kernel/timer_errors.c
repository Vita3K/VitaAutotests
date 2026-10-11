// Every timer call with a UID that isn't a timer it can use, and with arguments it should reject
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    const SceUID deleted = sceKernelCreateTimer("deleted", 0, NULL);
    sceKernelDeleteTimer(deleted);
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    const SceUID ids[] = { 0x12345, deleted, sema, 0, -1 };
    const char *const names[] = { "an unknown UID", "a deleted timer", "a semaphore", "UID 0", "UID -1" };

    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        SceUInt64 interval = 1000;
        SceKernelSysClock clock = 0;
        SceUInt32 waiting = 0xDEAD;
        TimerInfo info = { .size = sizeof(info) };
        PRINTF(sceKernelStartTimer(ids[i]), "start %s", names[i]);
        PRINTF(sceKernelStopTimer(ids[i]), "stop %s", names[i]);
        PRINTF(sceKernelGetTimerTime(ids[i], &clock), "get the time of %s", names[i]);
        PRINTF(sceKernelSetTimerTime(ids[i], &clock), "set the time of %s", names[i]);
        PRINTF(sceKernelGetTimerBase(ids[i], &clock), "get the base of %s", names[i]);
        PRINTF(sceKernelSetTimerEvent(ids[i], 1, &interval, 0), "set an event on %s", names[i]);
        PRINTF(sceKernelCancelTimer(ids[i], &waiting), "cancel %s", names[i]);
        PRINTF(sceKernelGetTimerInfo(ids[i], &info), "info of %s", names[i]);
        PRINTF(sceKernelCloseTimer(ids[i]), "close %s", names[i]);
        PRINTF(sceKernelDeleteTimer(ids[i]), "delete %s", names[i]);
    }
    PRINT("the semaphore survives the timer calls", sceKernelPollSema(sema, 1));

    // Creation
    PRINT("create with an empty name", test_created(sceKernelCreateTimer("", 0, NULL)));
    PRINT("create with attribute 0x80000000", test_created(sceKernelCreateTimer("t", 0x80000000, NULL)));
    PRINT("create with the openable attribute", test_created(sceKernelCreateTimer("t", ATTR_OPENABLE, NULL)));
    PRINT("create with the priority attribute", test_created(sceKernelCreateTimer("t", ATTR_TH_PRIO, NULL)));

    const SceUID timer = sceKernelCreateTimer("timer", 0, NULL);

    // Starting and stopping
    PRINT("stop a timer that isn't running", sceKernelStopTimer(timer));
    PRINT("start it", sceKernelStartTimer(timer));
    PRINT("start it again", sceKernelStartTimer(timer));
    SceKernelSysClock clock = 0;
    PRINT("set the time of a running timer", sceKernelSetTimerTime(timer, &clock));
    PRINT("stop it", sceKernelStopTimer(timer));
    SceUInt32 waiting = 0xDEAD;
    PRINT("cancel it", sceKernelCancelTimer(timer, &waiting));
    PRINT("its waiting count", waiting);
    PRINT("cancel without a waiting count pointer", sceKernelCancelTimer(timer, NULL));

    // Info
    TimerInfo info = { .size = sizeof(info) };
    PRINT("info", sceKernelGetTimerInfo(timer, &info));
    PRINT("its UID", info.timerId == timer);
    PRINT("its attribute", info.attr);
    PRINT("it isn't active", info.active);
    info.size = 0;
    PRINT("info with a size of 0", sceKernelGetTimerInfo(timer, &info));
    // With room behind the struct, in case the call writes as much as the size says
    static union {
        TimerInfo info;
        unsigned char bytes[sizeof(TimerInfo) + 64];
    } big;
    sceClibMemset(&big, 0xEE, sizeof(big));
    big.info.size = sizeof(TimerInfo) + 4;
    PRINT("info with a size past the struct", sceKernelGetTimerInfo(timer, &big.info));
    int untouched = 1;
    for (unsigned int i = sizeof(TimerInfo); i < sizeof(big.bytes); i++)
        untouched &= big.bytes[i] == 0xEE;
    PRINT("it leaves the bytes past the struct alone", untouched);

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("get the time into a NULL pointer", sceKernelGetTimerTime(timer, NULL));
    PRINT("set the time from a NULL pointer", sceKernelSetTimerTime(timer, NULL));
    PRINT("get the base into a NULL pointer", sceKernelGetTimerBase(timer, NULL));
    PRINT("info with a NULL pointer", sceKernelGetTimerInfo(timer, NULL));
    PRINT("open with a NULL name", sceKernelOpenTimer(NULL));
    PRINT("create with a NULL name", test_created(sceKernelCreateTimer(NULL, 0, NULL)));

    test_exit();
    return 0;
}
