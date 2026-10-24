// The wait UID thread info reports for a thread that delays, with and without callbacks, next to
// the one for a thread that waits for a thread signal. Neither is a UID. The test prints the delay's
// value, and for the signal wait only whether it is -1, since that value differs between consoles.
// Then whether a delay and a signal wait report the same one.
//
// On a PS Vita TV on firmware 3.65 a delay reports wait UID 0xFFFFFFFF, with callbacks too, and a
// signal wait another value.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int sceKernelWaitSignal(SceUInt32 unk0, SceUInt32 delay, SceUInt32 *timeout);

static volatile int kind;

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 300000;
    if (kind == 0)
        sceKernelDelayThread(300000);
    else if (kind == 1)
        sceKernelDelayThreadCB(300000);
    else
        sceKernelWaitSignal(0, 0, &timeout);
    return 0;
}

static int wait_id_of(int k, SceUInt32 *wait_type) {
    kind = k;
    const SceUID thread = test_start_thread(waiter);
    test_wait_until_waiting(thread);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    test_join_thread(thread);
    *wait_type = info.waitType;
    return info.waitId;
}

int main(void) {
    SceUInt32 delay_type, delay_cb_type, signal_type;
    const int delay = wait_id_of(0, &delay_type);
    const int delay_cb = wait_id_of(1, &delay_cb_type);
    const int signal = wait_id_of(2, &signal_type);
    PRINT("delay: the wait type", delay_type);
    PRINT("delay: the wait UID", delay);
    PRINT("delay with callbacks: the wait type", delay_cb_type);
    PRINT("delay with callbacks: the wait UID", delay_cb);
    PRINT("signal wait: the wait type", signal_type);
    PRINT("signal wait: the wait UID is -1", signal == -1);
    PRINT("a delay reports the wait UID of a signal wait", delay == signal);
    PRINT("a delay with callbacks reports the wait UID of a plain delay", delay_cb == delay);
    test_exit();
    return 0;
}
