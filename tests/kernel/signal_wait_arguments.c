// The first two arguments of a thread signal wait, unk0 and delay, which the firmware stores
// without a known reader. A signal already sent is taken with timeouts of 0 and 1, plain and with
// callbacks, and each wait prints its result, whether it left the timeout as it was, and the result
// of a second wait that has no signal left. A waiter blocked with three pairs of arguments is ended
// by one signal, and prints its result and whether it returned within 500 ms of the send. Last, a
// wait of 800 ms with a delay argument of 2 s and no signal, which ends at its timeout unless the
// delay comes first. Every wait has a finite timeout, and every case reads what it prints before it
// prints.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

#define W_PRIORITY 0xA0

static volatile SceUInt32 unk0_arg, delay_arg;
static volatile SceUInt64 sent_at, returned_at;
static volatile int woke, wait_result;

// Waits for a signal with the arguments it is given, and records when it returned
static int signal_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    wait_result = sceKernelWaitSignal(unk0_arg, delay_arg, &timeout);
    returned_at = sceKernelGetProcessTimeWide();
    woke = 1;
    return 0;
}

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

// Sends the calling thread a signal, then waits for it with a timeout, plain or with callbacks, and
// waits again with the same timeout
static void pending_signal(const char *name, int with_callbacks, SceUInt32 timeout_us) {
    const SceUID self = sceKernelGetThreadId();
    sceKernelSendSignal(self);
    SceUInt32 timeout = timeout_us;
    const int result = with_callbacks ? sceKernelWaitSignalCB(0, 0, &timeout) : sceKernelWaitSignal(0, 0, &timeout);
    PRINTF(result, "%s: the wait with a signal pending", name);
    PRINTF(timeout == timeout_us, "%s: the timeout is untouched", name);
    timeout = timeout_us;
    const int second = with_callbacks ? sceKernelWaitSignalCB(0, 0, &timeout) : sceKernelWaitSignal(0, 0, &timeout);
    PRINTF(second, "%s: a second wait", name);
}

// Starts a waiter with unk0 and delay, waits until it is in a signal wait, sends it one signal, and
// prints its result and whether it returned within 500 ms of the send
static void blocked_waiter(const char *name, SceUInt32 unk0, SceUInt32 delay) {
    unk0_arg = unk0;
    delay_arg = delay;
    woke = 0;
    wait_result = 0xDEAD;
    returned_at = 0;
    const SceUID w = sceKernelCreateThread("w", signal_entry, W_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(wait_type_of(w) == WAIT_TYPE_SIGNAL);
    sent_at = sceKernelGetProcessTimeWide();
    PRINTF(sceKernelSendSignal(w), "%s: send it a signal", name);
    TEST_WAIT_FOR(woke);
    PRINTF(wait_result, "%s: its wait", name);
    PRINTF(returned_at - sent_at < 500000, "%s: it returned within 500 ms of the send", name);
    if (woke)
        test_join_thread(w);
}

// A wait of 800 ms with a delay argument of 2 s and no signal
static void delay_argument_timeout(void) {
    SceUInt32 timeout = 800000;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    const int result = sceKernelWaitSignal(0, 2000000, &timeout);
    const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - start;
    PRINT("a wait of 800 ms with a delay argument of 2 s: the wait", result);
    PRINT("a wait of 800 ms with a delay argument of 2 s: it ended within [550 ms, 1300 ms)", elapsed >= 550000 && elapsed < 1300000);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    pending_signal("a plain wait with a timeout of 0", 0, 0);
    pending_signal("a plain wait with a timeout of 1", 0, 1);
    pending_signal("a wait with callbacks and a timeout of 0", 1, 0);
    pending_signal("a wait with callbacks and a timeout of 1", 1, 1);

    blocked_waiter("unk0 0 and delay 1 s", 0, 1000000);
    blocked_waiter("unk0 1 and delay 0xFFFFFFFF", 1, 0xFFFFFFFF);
    blocked_waiter("unk0 0xFFFFFFFF and delay 2 s", 0xFFFFFFFF, 2000000);

    delay_argument_timeout();

    test_exit();
    return 0;
}
