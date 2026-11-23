// What a notify of the pipe's own event does to the senders waiting on that pipe. A pipe of 4096
// bytes, queueing senders in arrival order, is full. Thread A has a callback registered to the
// pipe's event and waits in sceKernelSendMsgPipeCB, FULL, for 100 bytes. Thread B has no callback
// and waits behind A in sceKernelSendMsgPipe, FULL, for 8 bytes. Then this thread receives 64
// bytes, which frees enough room for B and not for A. A receive fires the pipe's event with
// notification, so A's callback is notified and A leaves its wait, runs the callback and waits
// again, now behind B. Each case runs on a fresh pipe and prints the receive's result, the runs of
// A's callback, whether A and B wait, the pipe's waiting senders and the free size, once A waits
// again. The cases:
// - walk, end by a receive: the callback gets unregistered, then a receive of 64 bytes ends the
//   waits. It prints both results and sizes and the free size.
// - walk, zero-size calls, then a cancel: the callback gets unregistered, then a send of 0 bytes
//   and a receive of 0 bytes run. It prints their results, whether B still waits and the waiting
//   senders, then a cancel of the pipe ends the waits. It prints both results and sizes.
// - direct notify, end by a receive: the callback gets unregistered before the receive, so the
//   receive notifies nothing. Then sceKernelNotifyCallback notifies A's callback directly. The
//   output is the same as the first case, and the end is the same receive.
//
// On a PS Vita TV on firmware 3.65 the receive returns 0 and the callback runs once. After
// the pipe's event notified A, A waits again behind B and B is not served, though 64 bytes are free
// for its 8, since the notify of an event that A waits on does not serve the pipe's senders after
// A left. So both wait, the pipe has 2 waiting senders and 0x40 bytes are free. A zero-size send and
// a zero-size receive return 0 at once and don't serve B either. The next receive of 64 bytes
// serves B first and then A, both with result 0, sizes 8 and 100. A cancel gives both the cancel
// error and size 0. With the direct notify A's departure does serve B, so B ends with result 0 and
// size 8 right after, and only A waits, so the pipe has 1 waiting sender and 0x38 bytes are free.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define A_SIZE 100
#define B_SIZE 8
#define FREED 64
#define NOT_DONE 0xDEAD
#define LONG_TIMEOUT (3 * TEST_WAIT_US)

enum Notify { WALK, DIRECT };
enum End { RECEIVE, ZERO_AND_CANCEL };

static SceUID pipe;
static volatile SceUID callback;
static volatile int callback_runs;
static volatile int result[2];
static SceSize moved[2];
static char fill[CAPACITY];
static char buffer[CAPACITY];

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    return 0;
}

static int sender_a(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = LONG_TIMEOUT;
    const SceUID own = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    sceKernelRegisterCallbackToEvent(pipe, own);
    callback = own;
    result[0] = sceKernelSendMsgPipeCB(pipe, buffer, A_SIZE, MSG_PIPE_MODE_FULL, &moved[0], &timeout);
    sceKernelDeleteCallback(own);
    return 0;
}

static int sender_b(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = LONG_TIMEOUT;
    result[1] = sceKernelSendMsgPipe(pipe, buffer, B_SIZE, MSG_PIPE_MODE_FULL, &moved[1], &timeout);
    return 0;
}

static int waiting_senders(void) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    return info.numSendWaitThreads;
}

static int free_size(void) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    return info.freeSize;
}

static void run(const char *name, enum Notify notify, enum End end) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    sceKernelTrySendMsgPipe(pipe, fill, CAPACITY, MSG_PIPE_MODE_FULL, NULL);
    callback = 0;
    callback_runs = 0;
    for (int i = 0; i < 2; i++) {
        result[i] = NOT_DONE;
        moved[i] = NOT_DONE;
    }

    const SceUInt32 a_type = WAIT_TYPE_MSG_PIPE | WAIT_TYPE_CB;
    SceUID a = sceKernelCreateThread("a", sender_a, 0x60, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(a, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(a, a_type, pipe));
    SceUID b = sceKernelCreateThread("b", sender_b, 0x60, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(b, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(b, WAIT_TYPE_MSG_PIPE, pipe));
    const int both_wait = test_in_wait(a, a_type, pipe) && test_in_wait(b, WAIT_TYPE_MSG_PIPE, pipe);
    const int senders_before = waiting_senders();

    int unregister = NOT_DONE, direct = NOT_DONE;
    if (notify == DIRECT)
        unregister = sceKernelUnregisterCallbackFromEvent(pipe, callback);
    SceSize receive_size = NOT_DONE;
    SceUInt timeout = LONG_TIMEOUT;
    const int receive = sceKernelReceiveMsgPipe(pipe, buffer, FREED, MSG_PIPE_MODE_FULL, &receive_size, &timeout);
    if (notify == DIRECT)
        direct = sceKernelNotifyCallback(callback, 1);
    // A ran its callback and waits again. B either waits or was served, and only reading B's wait
    // is stable once A is back in its wait.
    TEST_WAIT_FOR(callback_runs >= 1 && test_in_wait(a, a_type, pipe));
    if (notify == DIRECT)
        TEST_WAIT_FOR(result[1] != NOT_DONE);
    const int runs = callback_runs;
    const int a_waits = test_in_wait(a, a_type, pipe);
    const int b_waits = test_in_wait(b, WAIT_TYPE_MSG_PIPE, pipe);
    const int b_result = result[1];
    const int senders = waiting_senders();
    const int free_after = free_size();

    int zero_send = NOT_DONE, zero_receive = NOT_DONE, b_waits_zero = 0, senders_zero = NOT_DONE, a_waits_zero = 0;
    SceSize zero_send_size = NOT_DONE, zero_receive_size = NOT_DONE;
    if (notify == WALK)
        sceKernelUnregisterCallbackFromEvent(pipe, callback);
    if (end == ZERO_AND_CANCEL) {
        timeout = LONG_TIMEOUT;
        zero_send = sceKernelSendMsgPipe(pipe, buffer, 0, MSG_PIPE_MODE_FULL, &zero_send_size, &timeout);
        timeout = LONG_TIMEOUT;
        zero_receive = sceKernelReceiveMsgPipe(pipe, buffer, 0, MSG_PIPE_MODE_ASAP, &zero_receive_size, &timeout);
        a_waits_zero = test_in_wait(a, a_type, pipe);
        b_waits_zero = test_in_wait(b, WAIT_TYPE_MSG_PIPE, pipe);
        senders_zero = waiting_senders();
    }
    int end_result = NOT_DONE, cancelled_senders = NOT_DONE, cancelled_receivers = NOT_DONE;
    SceSize end_size = NOT_DONE;
    if (end == RECEIVE) {
        timeout = LONG_TIMEOUT;
        end_result = sceKernelReceiveMsgPipe(pipe, buffer, FREED, MSG_PIPE_MODE_FULL, &end_size, &timeout);
    } else {
        end_result = sceKernelCancelMsgPipe(pipe, &cancelled_senders, &cancelled_receivers);
    }
    test_join_thread(a);
    test_join_thread(b);
    const int free_end = free_size();
    sceKernelDeleteMsgPipe(pipe);

    PRINTF(both_wait, "%s: A and B wait before the receive", name);
    PRINTF(senders_before, "%s: the waiting senders before it", name);
    if (notify == DIRECT) {
        PRINTF(unregister, "%s: unregister the callback", name);
        PRINTF(direct, "%s: the direct notify", name);
    }
    PRINTF(receive, "%s: the receive", name);
    PRINTF(receive_size, "%s: the receive's size", name);
    PRINTF(runs, "%s: runs of A's callback", name);
    PRINTF(a_waits, "%s: A waits again", name);
    PRINTF(b_waits, "%s: B still waits", name);
    PRINTF(b_result, "%s: B's result so far", name);
    PRINTF(senders, "%s: the waiting senders", name);
    PRINTF(free_after, "%s: the free size", name);
    if (end == ZERO_AND_CANCEL) {
        PRINTF(zero_send, "%s: the send of 0 bytes", name);
        PRINTF(zero_send_size, "%s: its size", name);
        PRINTF(zero_receive, "%s: the receive of 0 bytes", name);
        PRINTF(zero_receive_size, "%s: its size", name);
        PRINTF(a_waits_zero, "%s: A still waits after them", name);
        PRINTF(b_waits_zero, "%s: B still waits after them", name);
        PRINTF(senders_zero, "%s: the waiting senders after them", name);
        PRINTF(end_result, "%s: the cancel", name);
        PRINTF(cancelled_senders, "%s: the senders it counted", name);
        PRINTF(cancelled_receivers, "%s: the receivers it counted", name);
    } else {
        PRINTF(end_result, "%s: the ending receive", name);
        PRINTF(end_size, "%s: its size", name);
    }
    PRINTF(result[0], "%s: A's final result", name);
    PRINTF(moved[0], "%s: A's final size", name);
    PRINTF(result[1], "%s: B's final result", name);
    PRINTF(moved[1], "%s: B's final size", name);
    PRINTF(free_end, "%s: the free size at the end", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);

    run("walk, end by a receive", WALK, RECEIVE);
    run("walk, zero-size calls then a cancel", WALK, ZERO_AND_CANCEL);
    run("direct notify, end by a receive", DIRECT, RECEIVE);

    test_exit();
    return 0;
}
