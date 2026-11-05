// What a thread shows while it waits in sceGxmDisplayQueueFinish, and whether a callback notified
// meanwhile runs. The display queue callback blocks on a semaphore the test owns, so one entry
// stays queued. Thread F creates a callback, then calls sceGxmDisplayQueueFinish and waits for the
// queue to empty. Main polls until F waits, prints F's status and wait type, and notifies F's
// callback. Then it shows whether F still waits and whether the callback ran. Then main releases
// the display callback. F's finish returns and F stops on a semaphore before it makes any call that
// runs callbacks. The test shows whether the callback ran by then. Last, F checks its callbacks,
// which runs the one that was notified. First, a finish before sceGxmInitialize and one with an
// empty queue.
//
// On a PS Vita TV on firmware 3.65 a finish before sceGxmInitialize fails with 0x805B0000 and a
// finish with an empty queue gives 0. While the display callback holds the queue, F shows WAITING
// 0x8 with the wait type for an event flag (0x10) and a wait ID, and its finish hasn't returned.
// After the notify F still has wait type 0x10 and the callback has not run. After the release the
// finish returns 0 and the callback has still not run. The display callback ran once. The callback
// runs once F checks its callbacks.
#include <psp2/gxm.h>

#include "test.h"

static SceUID release_sema, go_sema;
static SceGxmSyncObject *old_buffer, *new_buffer;
static volatile int in_display_callback, display_callbacks;
static volatile int f_callback_ready, finish_returned, finish_result, callback_runs;
static SceUID f_callback;

static void display_callback(const void *data) {
    (void)data;
    display_callbacks++;
    in_display_callback = 1;
    // Bounded, so a test that goes wrong ends
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelWaitSema(release_sema, 1, &timeout);
}

static int initialize(void) {
    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = 2;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = 4;
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    return sceGxmInitialize(&init);
}

static int f_callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    return 0;
}

// Calls the finish, then waits for main before it checks its callbacks
static int f_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    f_callback = sceKernelCreateCallback("f", 0, f_callback_entry, NULL);
    f_callback_ready = 1;
    finish_result = sceGxmDisplayQueueFinish();
    finish_returned = 1;
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelWaitSema(go_sema, 1, &timeout);
    sceKernelCheckCallback();
    return 0;
}

int main(void) {
    PRINT("finish before initializing", sceGxmDisplayQueueFinish());
    PRINT("initialize libgxm", initialize());
    PRINT("finish with an empty queue", sceGxmDisplayQueueFinish());

    release_sema = sceKernelCreateSema("release", 0, 0, 1, NULL);
    go_sema = sceKernelCreateSema("go", 0, 0, 1, NULL);
    PRINT("create the old buffer's sync object", sceGxmSyncObjectCreate(&old_buffer));
    PRINT("create the new buffer's sync object", sceGxmSyncObjectCreate(&new_buffer));
    int data = 0;
    PRINT("add an entry", sceGxmDisplayQueueAddEntry(old_buffer, new_buffer, &data));
    TEST_WAIT_FOR(in_display_callback);
    PRINT("the display callback runs", in_display_callback);

    const SceUID f = sceKernelCreateThread("f", f_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    sceKernelStartThread(f, 0, NULL);
    TEST_WAIT_FOR(f_callback_ready);
    test_wait_until_waiting(f);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    PRINT("F's info", sceKernelGetThreadInfo(f, &info));
    PRINT("F's status while it finishes", info.status);
    PRINT("F's wait type while it finishes", info.waitType);
    PRINT("F's wait ID is set", info.waitId > 0);
    PRINT("finish returned while waiting", finish_returned);

    PRINT("notify F's callback", sceKernelNotifyCallback(f_callback, 1));
    // A callback that runs does so as soon as F is awake, so a short delay is enough
    sceKernelDelayThread(100000);
    info.size = sizeof(info);
    sceKernelGetThreadInfo(f, &info);
    PRINT("F's wait type after the notify", info.waitType);
    PRINT("callback ran during finish", callback_runs);

    sceKernelSignalSema(release_sema, 1);
    TEST_WAIT_FOR(finish_returned);
    PRINT("finish returned after the release", finish_returned);
    PRINT("finish's result", finish_result);
    PRINT("callback ran after finish", callback_runs);
    PRINT("the display callback ran once", display_callbacks);

    sceKernelSignalSema(go_sema, 1);
    test_join_thread(f);
    PRINT("callback ran after a check", callback_runs);

    PRINT("destroy the old buffer's sync object", sceGxmSyncObjectDestroy(old_buffer));
    PRINT("destroy the new buffer's sync object", sceGxmSyncObjectDestroy(new_buffer));
    PRINT("terminate libgxm", sceGxmTerminate());
    sceKernelDeleteSema(release_sema);
    sceKernelDeleteSema(go_sema);
    test_exit();
    return 0;
}
