// What a message pipe does when the head of its sender queue leaves, by its timeout or by a
// callback notify while it waits in a callback send. The pipe holds 4096 bytes and is filled with
// '.' bytes up to 8 free. The head H sends 10 bytes FULL, which doesn't fit. Behind it, S2 sends
// "ij" and S3 sends "klmn", both FULL, which would fit. Every thread is pinned to core 0. The main
// thread runs at 0x60 and the senders at 0x80, 0x88 and 0x90, so the queue is H, S2, S3 with attr
// 0x0 (first in first out) and with attr 0x1 (by priority). The cases:
// - attr 0x0 and attr 0x1, H leaves by its 600 ms timeout. S2 and S3 are served at that moment,
//   done with sizes 2 and 4 and time left above 4.5 s. The free size is 2, no sender waits, and
//   the bytes after the dots are "ijklmn", which a try receive takes once no sender waits.
// - attr 0x0 and attr 0x1, S2 leaves by its 600 ms timeout while H waits at the head. Nothing is
//   served, the free size stays 8, and S3 still waits 250 ms later.
// - attr 0x0, H waits in a callback send and its callback waits on a gate. The main thread
//   notifies the callback. S2 and S3 are served by H's departure, which the info shows while the
//   callback is still blocked. Then the gate opens, H's callback returns and H waits again with
//   one sender. A cancel ends H. This case goes last, since it is the riskiest one.
// Every sender waits with a 6 s timeout unless its case says otherwise, so no wait is unbounded.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define FILL_BYTES 4088
#define MAIN_PRIORITY 0x60
#define NOT_SET 0xDEAD
#define SHORT_TIMEOUT_US 600000
#define LONG_TIMEOUT_US 6000000
#define GATE_TIMEOUT_US 6000000
#define SETTLE_POLLS 20
// A sender served at once keeps nearly all of its 6 s timeout
#define SERVED_TIME_LEFT_US 4500000
// The wait of a thread in a message pipe send with callbacks
#define HEAD_WAIT (WAIT_TYPE_MSG_PIPE | WAIT_TYPE_CB)

static const int priorities[3] = { 0x80, 0x88, 0x90 };
static const char *const data[3] = { "ABCDEFGHIJ", "ij", "klmn" };

static SceUID pipe, gate, callback;
static SceUID threads[3];
static volatile int callback_ready, callback_runs;
static volatile int result[3], done[3];
static SceSize moved[3];
static SceUInt32 timeouts[3];
static char fill[CAPACITY], drained[CAPACITY];
// The bytes other than '.' that the pipe gave back
static char seen[64];
static int seen_len;

// Returns at once, or waits on the gate until the main thread signals it
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    SceUInt timeout = GATE_TIMEOUT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    callback_runs++;
    return 0;
}

static int sender(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    result[i] = sceKernelSendMsgPipe(pipe, (void *)data[i], sceClibStrnlen(data[i], 16),
                                     MSG_PIPE_MODE_FULL, &moved[i], &timeouts[i]);
    done[i] = 1;
    return 0;
}

// Creates its callback and sends its bytes with callbacks
static int head_callback_sender(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    callback_ready = 1;
    result[0] = sceKernelSendMsgPipeCB(pipe, (void *)data[0], sceClibStrnlen(data[0], 16),
                                       MSG_PIPE_MODE_FULL, &moved[0], &timeouts[0]);
    done[0] = 1;
    sceKernelDeleteCallback(callback);
    return 0;
}

// Starts thread i at its priority and returns once it waits on the pipe
static void start_thread(int i, SceKernelThreadEntry entry) {
    result[i] = NOT_SET;
    done[i] = 0;
    moved[i] = NOT_SET;
    threads[i] = sceKernelCreateThread("sender", entry, priorities[i], 0x4000, 0,
                                       SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(threads[i], sizeof(i), &i);
    test_wait_until_waiting(threads[i]);
}

static SceKernelMppInfo pipe_info(void) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    return info;
}

// Creates the pipe and fills it up to 8 free bytes, with nothing waiting
static void make_pipe(SceUInt32 attr) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, attr, CAPACITY, NULL);
    sceKernelTrySendMsgPipe(pipe, fill, FILL_BYTES, MSG_PIPE_MODE_FULL, NULL);
}

// Waits until S2 and S3 are done or wait on the pipe, on 20 polls in a row, at most 5 s. A thread
// that was just woken may show its wait until it runs, so one look isn't enough.
static void settle_behind(void) {
    int stable = 0;
    for (int poll = 0; poll < 5000 && stable < SETTLE_POLLS; poll++) {
        int all = 1;
        for (int i = 1; i < 3; i++)
            all = all && (done[i] || test_in_wait(threads[i], WAIT_TYPE_MSG_PIPE, pipe));
        stable = all ? stable + 1 : 0;
        if (stable < SETTLE_POLLS)
            sceKernelDelayThread(1000);
    }
}

// Copies the bytes other than '.' that a try receive of the whole pipe gives back into seen
static int drain_pipe(void) {
    SceSize size = 0;
    const int received =
        sceKernelTryReceiveMsgPipe(pipe, drained, CAPACITY, MSG_PIPE_MODE_ASAP, &size);
    seen_len = 0;
    for (SceSize i = 0; received == 0 && i < size; i++) {
        if (drained[i] != '.' && seen_len < (int)sizeof(seen) - 1)
            seen[seen_len++] = drained[i];
    }
    seen[seen_len] = '\0';
    return received;
}

// H leaves by its timeout, which serves S2 and S3 at that moment
static void head_times_out(const char *name, SceUInt32 attr) {
    make_pipe(attr);
    timeouts[0] = SHORT_TIMEOUT_US;
    timeouts[1] = LONG_TIMEOUT_US;
    timeouts[2] = LONG_TIMEOUT_US;
    start_thread(0, sender);
    start_thread(1, sender);
    start_thread(2, sender);
    test_join_thread(threads[0]);
    settle_behind();
    const SceKernelMppInfo info = pipe_info();
    test_join_thread(threads[1]);
    test_join_thread(threads[2]);
    const int received = drain_pipe();
    const int canceled = sceKernelCancelMsgPipe(pipe, NULL, NULL);

    PRINTF(result[0], "%s: H's result", name);
    PRINTF(moved[0], "%s: H's size", name);
    PRINTF(info.freeSize, "%s: the free size after H left", name);
    PRINTF(info.numSendWaitThreads, "%s: the waiting senders after H left", name);
    for (int i = 1; i < 3; i++) {
        PRINTF(done[i], "%s: S%d is done after H left", name, i + 1);
        PRINTF(result[i], "%s: S%d's result", name, i + 1);
        PRINTF(moved[i], "%s: S%d's size", name, i + 1);
        PRINTF(timeouts[i] > SERVED_TIME_LEFT_US, "%s: S%d's time left is above 4.5 s", name,
               i + 1);
    }
    PRINTF(received, "%s: the try receive of the pipe", name);
    PRINTF(seen_len, "%s: the pipe held \"%s\" past the '.' bytes", name, seen);
    PRINTF(canceled, "%s: cancel the pipe", name);
    sceKernelDeleteMsgPipe(pipe);
}

// S2 leaves by its timeout while H waits at the head, so the sender behind it isn't served
static void middle_times_out(const char *name, SceUInt32 attr) {
    make_pipe(attr);
    timeouts[0] = LONG_TIMEOUT_US;
    timeouts[1] = SHORT_TIMEOUT_US;
    timeouts[2] = LONG_TIMEOUT_US;
    start_thread(0, sender);
    start_thread(1, sender);
    start_thread(2, sender);
    test_join_thread(threads[1]);
    const SceKernelMppInfo after_exit = pipe_info();
    const int s3_waits_after_exit = test_in_wait(threads[2], WAIT_TYPE_MSG_PIPE, pipe);

    sceKernelDelayThread(250000);
    const SceKernelMppInfo later = pipe_info();
    const int h_waits = test_in_wait(threads[0], WAIT_TYPE_MSG_PIPE, pipe);
    const int s3_waits = test_in_wait(threads[2], WAIT_TYPE_MSG_PIPE, pipe);
    int canceled_senders = -1, canceled_receivers = -1;
    const int canceled = sceKernelCancelMsgPipe(pipe, &canceled_senders, &canceled_receivers);
    test_join_thread(threads[0]);
    test_join_thread(threads[2]);

    PRINTF(result[1], "%s: S2's result", name);
    PRINTF(moved[1], "%s: S2's size", name);
    PRINTF(after_exit.freeSize, "%s: the free size right after S2 left", name);
    PRINTF(after_exit.numSendWaitThreads, "%s: the waiting senders right after S2 left", name);
    PRINTF(s3_waits_after_exit, "%s: S3 waits right after S2 left", name);
    PRINTF(h_waits, "%s: H still waits 250 ms later", name);
    PRINTF(s3_waits, "%s: S3 still waits 250 ms later", name);
    PRINTF(later.freeSize, "%s: the free size 250 ms later", name);
    PRINTF(later.numSendWaitThreads, "%s: the waiting senders 250 ms later", name);
    PRINTF(canceled, "%s: cancel the pipe", name);
    PRINTF(canceled_senders, "%s: the senders the cancel ended", name);
    PRINTF(result[0], "%s: H's result", name);
    PRINTF(moved[0], "%s: H's size", name);
    PRINTF(result[2], "%s: S3's result", name);
    PRINTF(moved[2], "%s: S3's size", name);
    sceKernelDeleteMsgPipe(pipe);
}

// H waits in a callback send and its callback waits on a gate, then the main thread notifies it
static void head_leaves_to_callback(const char *name, SceUInt32 attr) {
    make_pipe(attr);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    callback_ready = 0;
    callback_runs = 0;
    result[0] = NOT_SET;
    done[0] = 0;
    moved[0] = NOT_SET;
    timeouts[0] = LONG_TIMEOUT_US;
    timeouts[1] = LONG_TIMEOUT_US;
    timeouts[2] = LONG_TIMEOUT_US;
    threads[0] = sceKernelCreateThread("head", head_callback_sender, priorities[0], 0x4000, 0,
                                       SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(threads[0], 0, NULL);
    TEST_WAIT_FOR(callback_ready && test_in_wait(threads[0], HEAD_WAIT, pipe));
    start_thread(1, sender);
    start_thread(2, sender);
    const int head_waits = test_in_wait(threads[0], HEAD_WAIT, pipe);

    const int notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(test_in_wait(threads[0], WAIT_TYPE_SEMA, gate));
    const int in_gate = test_in_wait(threads[0], WAIT_TYPE_SEMA, gate);
    // H's callback is blocked on the gate, so the pipe's state is stable once S2 and S3 settle
    settle_behind();
    const SceKernelMppInfo blocked = pipe_info();
    const int served = done[1] && done[2];

    sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(test_in_wait(threads[0], HEAD_WAIT, pipe));
    const int head_waits_again = test_in_wait(threads[0], HEAD_WAIT, pipe);
    const SceKernelMppInfo again = pipe_info();

    int canceled_senders = -1, canceled_receivers = -1;
    const int canceled = sceKernelCancelMsgPipe(pipe, &canceled_senders, &canceled_receivers);
    for (int i = 0; i < 3; i++)
        test_join_thread(threads[i]);
    sceKernelDeleteSema(gate);

    PRINTF(head_waits, "%s: H waits with callbacks before the notify", name);
    PRINTF(notify, "%s: the notify", name);
    PRINTF(in_gate, "%s: H's callback waits on the gate", name);
    PRINTF(served, "%s: S2 and S3 are done while H's callback waits", name);
    PRINTF(blocked.freeSize, "%s: the free size while H's callback waits", name);
    PRINTF(blocked.numSendWaitThreads, "%s: the waiting senders while H's callback waits", name);
    PRINTF(head_waits_again, "%s: H waits again after the gate opened", name);
    PRINTF(again.freeSize, "%s: the free size with H waiting again", name);
    PRINTF(again.numSendWaitThreads, "%s: the waiting senders with H waiting again", name);
    PRINTF(canceled, "%s: cancel the pipe", name);
    PRINTF(canceled_senders, "%s: the senders the cancel ended", name);
    PRINTF(result[0], "%s: H's result", name);
    PRINTF(moved[0], "%s: H's size", name);
    PRINTF(callback_runs, "%s: runs of H's callback", name);
    for (int i = 1; i < 3; i++) {
        PRINTF(result[i], "%s: S%d's result", name, i + 1);
        PRINTF(moved[i], "%s: S%d's size", name, i + 1);
        PRINTF(timeouts[i] > SERVED_TIME_LEFT_US, "%s: S%d's time left is above 4.5 s", name,
               i + 1);
    }
    sceKernelDeleteMsgPipe(pipe);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);
    sceClibMemset(fill, '.', sizeof(fill));

    head_times_out("attr 0x0, H times out", 0x0);
    head_times_out("attr 0x1, H times out", 0x1);
    middle_times_out("attr 0x0, S2 times out", 0x0);
    middle_times_out("attr 0x1, S2 times out", 0x1);
    head_leaves_to_callback("attr 0x0, H leaves to a callback", 0x0);

    test_exit();
    return 0;
}
