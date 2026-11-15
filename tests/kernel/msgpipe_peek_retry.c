// What a FULL receive with DONT_REMOVE gets that took part of its bytes from the pipe and waits for
// the rest, when its wait goes on again without a send. A pipe of 4096 bytes queues its receivers
// in order. Every thread is pinned to core 0, the main thread runs at 0x60 and the receivers at
// 0xB0. The main thread first puts "AB" into the empty pipe with a try send. Receiver R then waits
// for 4 bytes with FULL and DONT_REMOVE, so it holds "AB" and still wants 2. Two cases:
// - callback: R waits with sceKernelReceiveMsgPipeCB and a timeout of 4 s, and the main thread
//   notifies R's callback, which returns at once.
// - receiver leaves: R waits with sceKernelReceiveMsgPipe and a timeout of 4 s. Receiver Q then
//   waits behind it for 4 bytes in FULL mode with a timeout of 100 ms, and the main thread waits
//   for Q to end. Once Q ended, the main thread reads R's size and buffer while R may still wait,
//   then cancels the pipe.
// Each case prints the send, whether R waits after taking "AB", the notify and the runs of R's
// callback or Q's result, size and bytes, then whether R still waits, R's size and bytes at that
// point and the bytes in the pipe, then the cancel and R's result, size and bytes.
//
// On a PS Vita TV on firmware 3.65 R takes "AB" from the pipe again in both cases, since a peek
// leaves the pipe's read position where it was. After its callback R ends at once with 4 bytes,
// "ABAB". When Q leaves, R gets "AB" a second time and its size reads 4 while it still waits. The
// cancel then ends it with 0x80028007. The pipe holds its 2 bytes throughout.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define MAIN_PRIORITY 0x60
#define RECEIVER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define R_TIMEOUT_US 4000000
#define Q_TIMEOUT_US 100000

static SceUID pipe, callback;
static int with_callback;
static volatile int callback_ready, callback_runs, r_result, q_result;
static SceSize r_size, q_size;
static char r_buffer[5], q_buffer[5];

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    return 0;
}

// Peeks at 4 bytes in FULL mode, with callbacks in the callback case
static int r_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = R_TIMEOUT_US;
    const int mode = MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_REMOVE;
    if (with_callback) {
        callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
        callback_ready = 1;
        r_result = sceKernelReceiveMsgPipeCB(pipe, r_buffer, 4, mode, &r_size, &timeout);
        sceKernelDeleteCallback(callback);
    } else {
        callback_ready = 1;
        r_result = sceKernelReceiveMsgPipe(pipe, r_buffer, 4, mode, &r_size, &timeout);
    }
    return 0;
}

// Receives 4 bytes in FULL mode for a short time
static int q_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = Q_TIMEOUT_US;
    q_result = sceKernelReceiveMsgPipe(pipe, q_buffer, 4, MSG_PIPE_MODE_FULL, &q_size, &timeout);
    return 0;
}

static int pipe_used(void) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    return CAPACITY - info.freeSize;
}

static void run_case(const char *name, int use_callback) {
    with_callback = use_callback;
    callback_ready = callback_runs = 0;
    r_result = q_result = NOT_DONE;
    r_size = q_size = NOT_DONE;
    sceClibMemset(r_buffer, 0, sizeof(r_buffer));
    sceClibMemset(q_buffer, 0, sizeof(q_buffer));
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);

    SceSize sent = NOT_DONE;
    const int send = sceKernelTrySendMsgPipe(pipe, "AB", 2, MSG_PIPE_MODE_FULL, &sent);

    const SceUInt32 r_wait_type = WAIT_TYPE_MSG_PIPE | (use_callback ? WAIT_TYPE_CB : 0);
    const SceUID r = sceKernelCreateThread("r", r_entry, RECEIVER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(r, 0, NULL);
    TEST_WAIT_FOR(callback_ready && test_in_wait(r, r_wait_type, pipe));
    const int r_waits = test_in_wait(r, r_wait_type, pipe);

    int notify = NOT_DONE;
    if (use_callback) {
        notify = sceKernelNotifyCallback(callback, 1);
        TEST_WAIT_FOR(callback_runs > 0);
        // R either ended or waits again after its callback
        TEST_WAIT_FOR(r_result != NOT_DONE || test_in_wait(r, r_wait_type, pipe));
    } else {
        const SceUID q = sceKernelCreateThread("q", q_entry, RECEIVER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(q, 0, NULL);
        test_join_thread(q);
    }
    const int r_still_waits = test_in_wait(r, r_wait_type, pipe);
    // Read while R may still wait, its size and buffer are written by whoever moves its bytes
    const int size_then = r_size;
    char buffer_then[5] = { 0 };
    sceClibMemcpy(buffer_then, r_buffer, 4);
    const int used = pipe_used();

    const int cancel = sceKernelCancelMsgPipe(pipe, NULL, NULL);
    test_join_thread(r);
    sceKernelDeleteMsgPipe(pipe);

    PRINTF(send, "%s: try send \"AB\"", name);
    PRINTF(sent, "%s: try send \"AB\", its size", name);
    PRINTF(r_waits, "%s: R waits after taking \"AB\"", name);
    if (use_callback) {
        PRINTF(notify, "%s: the notify", name);
        PRINTF(callback_runs, "%s: runs of R's callback", name);
    } else {
        PRINTF(q_result, "%s: Q's receive", name);
        PRINTF(q_size, "%s: Q got \"%s\", its size", name, q_buffer);
    }
    PRINTF(r_still_waits, "%s: R still waits then", name);
    PRINTF(size_then, "%s: R then holds \"%s\", its size", name, buffer_then);
    PRINTF(used, "%s: the bytes in the pipe then", name);
    PRINTF(cancel, "%s: the cancel", name);
    PRINTF(r_result, "%s: R's receive", name);
    PRINTF(r_size, "%s: R got \"%s\", its size", name, r_buffer);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("callback", 1);
    run_case("receiver leaves", 0);

    test_exit();
    return 0;
}
