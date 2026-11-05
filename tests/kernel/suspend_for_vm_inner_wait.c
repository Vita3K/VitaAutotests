// What a ForVM suspend does to a thread whose callback is blocked in a wait of its own. T has the
// VM attribute, runs on core 0 and waits on a semaphore with callbacks. Its callback sets a flag,
// delays for 500 ms and sets a second flag. The main thread on core 1 notifies the callback, waits
// until T is in the delay and suspends T. It prints:
// - T's status and wait type right after the suspend, while the delay still runs
// - T's status and wait type once the delay is over, whether the callback got past the delay, and
//   whether the delay has returned a result to the callback
// - after the resume, whether the callback got past the delay, the runs of the callback and T's
//   wait type once it waits on the semaphore again, then the wait's result
//
// On a PS Vita TV on firmware 3.65 the suspend returns 1 and T still shows WAITING 0x8 with wait
// type 0x1, the delay. Once the delay is over T shows SUSPENDED 0x100 with wait type 0, the
// callback has not got past the delay and the delay has returned no result. After the resume, which
// returns 0, the callback gets past the delay and the delay returns 0. The callback runs once, T
// waits on the semaphore again with wait type 0x80000020 and its wait returns 0.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define T_PRIORITY 0xA0

// Far longer than the main thread needs to suspend T, far shorter than the wait's timeout
#define DELAY_US 500000

static SceUID sema, callback;
static volatile int ran, in_callback, after_delay, delay_result, cb_calls, wait_result;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    cb_calls++;
    in_callback = 1;
    delay_result = sceKernelDelayThread(DELAY_US);
    after_delay = 1;
    return 0;
}

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("cb", 0, callback_entry, NULL);
    ran = 1;
    SceUInt timeout = 3 * TEST_WAIT_US;
    wait_result = sceKernelWaitSemaCB(sema, 1, &timeout);
    return 0;
}

static SceKernelThreadInfo info_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    ran = in_callback = after_delay = cb_calls = 0;
    delay_result = wait_result = 0xDEAD;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID t = sceKernelCreateThread("t", t_entry, T_PRIORITY, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(ran);
    TEST_WAIT_FOR(info_of(t).waitType == (WAIT_TYPE_CB | WAIT_TYPE_SEMA));

    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(in_callback && info_of(t).waitType == WAIT_TYPE_DELAY);
    const int suspended = sceKernelSuspendThreadForVM(t);
    const SceKernelThreadInfo during = info_of(t);

    // The delay ends in 500 ms, and a held T shows status 0x100
    TEST_WAIT_FOR(info_of(t).status == 0x100);
    sceKernelDelayThread(50000);
    const SceKernelThreadInfo after = info_of(t);
    const int passed_delay_held = after_delay;
    const int result_unset = delay_result == 0xDEAD;

    const int resumed = sceKernelResumeThreadForVM(t);
    TEST_WAIT_FOR(after_delay);
    TEST_WAIT_FOR(info_of(t).waitType == (WAIT_TYPE_CB | WAIT_TYPE_SEMA));
    const int passed_delay = after_delay, calls = cb_calls;
    const SceUInt32 type_again = info_of(t).waitType;
    sceKernelSignalSema(sema, 1);
    test_join_thread(t);
    sceKernelDeleteSema(sema);

    PRINT("suspend it", suspended);
    PRINT("while its delay runs: its status", during.status);
    PRINT("while its delay runs: its wait type", during.waitType);
    PRINT("after its delay ended: its status", after.status);
    PRINT("after its delay ended: its wait type", after.waitType);
    PRINT("after its delay ended: the callback got past the delay", passed_delay_held);
    PRINT("after its delay ended: the delay has returned a result", !result_unset);
    PRINT("resume it", resumed);
    PRINT("after the resume: the callback got past the delay", passed_delay);
    PRINT("after the resume: the runs of the callback", calls);
    PRINT("after the resume: its wait type once it waits again", type_again);
    PRINT("after the resume: the delay's result", delay_result);
    PRINT("its semaphore wait's result", wait_result);
    test_exit();
    return 0;
}
