// A thread with a callback pending and a signal pending calls sceKernelWaitSignalCB. The callback
// runs first, at the top of the wait's loop, and inside it sends a signal to the thread itself and
// records the result of that send. A send fails while a signal is still pending, so the recorded
// result tells whether the pending signal was taken before the callback ran. The test prints that
// result, the wait's result, how often the callback ran, and whether a second sceKernelWaitSignal
// with a timeout of 0 finds a signal. Both signals and the callback are set up before the wait.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

#define WAIT_US 1000000

static SceUID self, callback;
static volatile int callback_runs;
// Stays at this value when the callback never ran
static volatile int send_result = 0xDEAD;

// Counts its runs and sends a signal to the thread it runs on, recording the result
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    send_result = sceKernelSendSignal(self);
    return 0;
}

int main(void) {
    self = sceKernelGetThreadId();
    callback = sceKernelCreateCallback("signal callback", 0, callback_entry, NULL);
    sceKernelSendSignal(self);
    sceKernelNotifyCallback(callback, 1);

    SceUInt32 timeout = WAIT_US;
    const int result = sceKernelWaitSignalCB(0, 0, &timeout);
    const int runs = callback_runs;
    SceUInt32 zero = 0;
    const int second = sceKernelWaitSignal(0, 0, &zero);
    sceKernelDeleteCallback(callback);

    PRINT("runs of the callback", runs);
    PRINT("the callback's send to its own thread", send_result);
    PRINT("the wait with callbacks", result);
    PRINT("a second wait for a signal with timeout 0", second);

    test_exit();
    return 0;
}
