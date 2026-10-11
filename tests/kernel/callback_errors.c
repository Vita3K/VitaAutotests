// Every callback call with a UID that isn't a callback it can use, and with arguments it should
// reject
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID other_callback, created;

static int callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    return 0;
}

// Creates a callback of its own and waits until created is signaled
static int other_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    other_callback = sceKernelCreateCallback("other", 0, callback, NULL);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(created, 1, &timeout);
    return 0;
}

int main(void) {
    const SceUID deleted = sceKernelCreateCallback("deleted", 0, callback, NULL);
    sceKernelDeleteCallback(deleted);
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    const SceUID ids[] = { 0x12345, deleted, sema, 0, -1 };
    const char *const names[] = { "an unknown UID", "a deleted callback", "a semaphore", "UID 0", "UID -1" };

    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        CallbackInfo info = { .size = sizeof(info) };
        PRINTF(sceKernelNotifyCallback(ids[i], 1), "notify %s", names[i]);
        PRINTF(sceKernelCancelCallback(ids[i]), "cancel %s", names[i]);
        PRINTF(sceKernelGetCallbackCount(ids[i]), "the count of %s", names[i]);
        PRINTF(sceKernelGetCallbackInfo(ids[i], (SceKernelCallbackInfo *)&info), "info of %s", names[i]);
        PRINTF(sceKernelDeleteCallback(ids[i]), "delete %s", names[i]);
    }
    PRINT("the semaphore survives the callback calls", sceKernelPollSema(sema, 1));

    // Creation
    PRINT("create with an empty name", test_created(sceKernelCreateCallback("", 0, callback, NULL)));
    PRINT("create with attribute 0x80000000", test_created(sceKernelCreateCallback("c", 0x80000000, callback, NULL)));
    PRINT("create with attribute 0x1", test_created(sceKernelCreateCallback("c", 0x1, callback, NULL)));

    // A callback of another thread
    created = sceKernelCreateSema("created", 0, 0, 1, NULL);
    const SceUID other = test_start_thread(other_thread);
    test_wait_until_waiting(other);
    PRINT("notify another thread's callback", sceKernelNotifyCallback(other_callback, 1));
    PRINT("its count", sceKernelGetCallbackCount(other_callback));
    PRINT("check callbacks here", sceKernelCheckCallback());
    PRINT("its count after the check", sceKernelGetCallbackCount(other_callback));
    PRINT("cancel it", sceKernelCancelCallback(other_callback));
    PRINT("delete it", sceKernelDeleteCallback(other_callback));
    sceKernelSignalSema(created, 1);
    test_join_thread(other);

    // Notifications and the count
    const SceUID cb = sceKernelCreateCallback("cb", 0, callback, NULL);
    PRINT("notify with argument 0", sceKernelNotifyCallback(cb, 0));
    PRINT("notify with a negative argument", sceKernelNotifyCallback(cb, -1));
    PRINT("the count", sceKernelGetCallbackCount(cb));
    PRINT("cancel", sceKernelCancelCallback(cb));
    PRINT("the count after the cancel", sceKernelGetCallbackCount(cb));
    PRINT("cancel again", sceKernelCancelCallback(cb));
    PRINT("check callbacks with none pending", sceKernelCheckCallback());

    // Info
    CallbackInfo info = { .size = 0 };
    PRINT("info with a size of 0", sceKernelGetCallbackInfo(cb, (SceKernelCallbackInfo *)&info));
    // With room behind the struct, in case the call writes as much as the size says
    static union {
        CallbackInfo info;
        unsigned char bytes[sizeof(CallbackInfo) + 64];
    } big;
    sceClibMemset(&big, 0xEE, sizeof(big));
    big.info.size = sizeof(CallbackInfo) + 4;
    PRINT("info with a size past the struct", sceKernelGetCallbackInfo(cb, (SceKernelCallbackInfo *)&big.info));
    int untouched = 1;
    for (unsigned int i = sizeof(CallbackInfo); i < sizeof(big.bytes); i++)
        untouched &= big.bytes[i] == 0xEE;
    PRINT("it leaves the bytes past the struct alone", untouched);
    PRINT("delete it", sceKernelDeleteCallback(cb));
    PRINT("delete it again", sceKernelDeleteCallback(cb));

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    const SceUID last = sceKernelCreateCallback("last", 0, callback, NULL);
    PRINT("info with a NULL pointer", sceKernelGetCallbackInfo(last, NULL));
    PRINT("create with a NULL function", test_created(sceKernelCreateCallback("c", 0, NULL, NULL)));
    PRINT("create with a NULL name", test_created(sceKernelCreateCallback(NULL, 0, callback, NULL)));

    test_exit();
    return 0;
}
