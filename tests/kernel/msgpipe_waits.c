// Message pipe senders: waiting for room, partial sends, size limits, and ending their waits
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// A Vita rounds a pipe's buffer up to 4 KiB, so the pipe is created with that size
#define CAPACITY 4096
// Leaves 4 bytes free, not enough for the 8 a sender sends
#define FILL (CAPACITY - 4)

static SceUID pipe;
static int send_result;
static unsigned int sent;

// Sends 8 bytes, waiting until all of them fit
static int send_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sent = 0;
    send_result = sceKernelSendMsgPipe(pipe, "12345678", 8, MSG_PIPE_MODE_FULL, &sent, &timeout);
    return 0;
}

static SceUID start_sender(void) {
    send_result = -1;
    const SceUID thread = test_start_thread(send_thread);
    test_wait_until_waiting(thread);
    return thread;
}

int main(void) {
    char buf[CAPACITY * 2] = { 0 };
    SceSize size;
    unsigned int got = 0;
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);

    PRINT("send more than the pipe holds", sceKernelSendMsgPipe(pipe, buf, CAPACITY + 1, MSG_PIPE_MODE_ASAP, &got, NULL));
    PRINT("receive more than the pipe holds", sceKernelReceiveMsgPipe(pipe, buf, CAPACITY + 1, MSG_PIPE_MODE_ASAP, &got, NULL));

    // ASAP sends what fits
    sceKernelTrySendMsgPipe(pipe, buf, FILL, MSG_PIPE_MODE_FULL, &size);
    PRINT("send ASAP of more than fits", sceKernelSendMsgPipe(pipe, buf, 8, MSG_PIPE_MODE_ASAP, &got, NULL));
    PRINT("send ASAP sends what fits", got);
    sceKernelTryReceiveMsgPipe(pipe, buf, CAPACITY, MSG_PIPE_MODE_FULL, &size);

    // A FULL send waits until all of it fits
    sceKernelTrySendMsgPipe(pipe, buf, FILL, MSG_PIPE_MODE_FULL, &size);
    SceUID sender = start_sender();
    PRINT("the sender waits while it doesn't fit", send_result == -1);
    sceKernelTryReceiveMsgPipe(pipe, buf, 8, MSG_PIPE_MODE_FULL, &size);
    test_join_thread(sender);
    PRINT("a receive that makes room ends the wait", send_result);
    PRINT("the sender sent all of it", sent);
    sceKernelTryReceiveMsgPipe(pipe, buf, FILL - 8, MSG_PIPE_MODE_FULL, &size);
    PRINT("the data arrives after what was there", sceKernelTryReceiveMsgPipe(pipe, buf, 8, MSG_PIPE_MODE_FULL, &size));
    PRINT("in order", buf[0] == '1' && buf[7] == '8');

    // Canceling and deleting end the wait of a sender
    sceKernelTrySendMsgPipe(pipe, buf, FILL, MSG_PIPE_MODE_FULL, &size);
    sender = start_sender();
    int waiting_senders = -1, waiting_receivers = -1;
    PRINT("cancel", sceKernelCancelMsgPipe(pipe, &waiting_senders, &waiting_receivers));
    test_join_thread(sender);
    PRINT("cancel counts the waiting senders", waiting_senders);
    PRINT("cancel counts the waiting receivers", waiting_receivers);
    PRINT("a canceled send", send_result);
    PRINT("cancel empties the pipe", sceKernelTryReceiveMsgPipe(pipe, buf, 1, MSG_PIPE_MODE_ASAP, &size));

    sceKernelTrySendMsgPipe(pipe, buf, FILL, MSG_PIPE_MODE_FULL, &size);
    sender = start_sender();
    PRINT("delete", sceKernelDeleteMsgPipe(pipe));
    test_join_thread(sender);
    PRINT("a send on a deleted pipe", send_result);

    test_exit();
    return 0;
}
