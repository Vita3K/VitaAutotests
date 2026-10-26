// A thread started again, where each run makes its own callback. The first run makes callback C1
// and ends, which takes the callback with it. The second run makes C2 and waits on a semaphore
// without callbacks, so the notifies the main thread sends while it waits stay pending until the
// run calls sceKernelCheckCallback. The cases: whether C2 got C1's UID back, the two notify results,
// whether C2's info names the thread, C2's runs, the check's result, and C1's runs.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID go, made[2];
static volatile int runs, calls[2], check_result;

// Counts the runs of the callback of each run, which is passed as common
static int counting_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    calls[(int)(SceUIntPtr)common]++;
    return 0;
}

// Makes the callback of its run. The second run waits without callbacks, then checks them.
static int owner_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    const int run = runs++;
    made[run] = sceKernelCreateCallback("owner", 0, counting_callback, (void *)(SceUIntPtr)run);
    if (run == 1) {
        SceUInt timeout = TEST_WAIT_US;
        sceKernelWaitSema(go, 1, &timeout);
        check_result = sceKernelCheckCallback();
    }
    return 0;
}

int main(void) {
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    const SceUID thread = sceKernelCreateThread("owner", owner_thread, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);

    // The first run makes C1 and ends
    sceKernelStartThread(thread, 0, NULL);
    sceKernelWaitThreadEnd(thread, NULL, NULL);

    // The second run makes C2 and waits
    sceKernelStartThread(thread, 0, NULL);
    test_wait_until_waiting(thread);
    PRINT("the second run's callback has the first run's UID", made[1] == made[0]);
    PRINT("notify the first run's callback", sceKernelNotifyCallback(made[0], 1));
    PRINT("notify the second run's callback", sceKernelNotifyCallback(made[1], 1));
    CallbackInfo info = { .size = sizeof(info) };
    PRINT("the second run's callback info", sceKernelGetCallbackInfo(made[1], (SceKernelCallbackInfo *)&info));
    PRINT("the second run's callback names the thread", info.threadId == thread);

    // The second run checks its callbacks once it gets its signal
    sceKernelSignalSema(go, 1);
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    PRINT("the second run's check", check_result);
    PRINT("the second run's callback runs", calls[1]);
    PRINT("the first run's callback runs", calls[0]);
    sceKernelDeleteThread(thread);

    test_exit();
    return 0;
}
