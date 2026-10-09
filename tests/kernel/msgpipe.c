#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID pipe;
static char received[4];
static int receive_result;

static int receive_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    receive_result = sceKernelReceiveMsgPipe(pipe, received, sizeof(received), MSG_PIPE_MODE_FULL, NULL, NULL);
    return 0;
}

int main(void) {
    char buf[4096] = { 0 };
    SceSize size = 0xDEAD;
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, sizeof(buf), NULL);

    PRINT("try receive on an empty pipe", sceKernelTryReceiveMsgPipe(pipe, buf, 1, MSG_PIPE_MODE_ASAP, &size));
    PRINT("try send that fills the pipe", sceKernelTrySendMsgPipe(pipe, buf, sizeof(buf), MSG_PIPE_MODE_FULL, &size));
    PRINT("try send on a full pipe", sceKernelTrySendMsgPipe(pipe, buf, 1, MSG_PIPE_MODE_ASAP, &size));
    PRINT("try receive that empties the pipe", sceKernelTryReceiveMsgPipe(pipe, buf, sizeof(buf), MSG_PIPE_MODE_FULL, &size));
    PRINT("try receive on an unknown pipe", sceKernelTryReceiveMsgPipe(0x12345, buf, 1, MSG_PIPE_MODE_ASAP, &size));

    // Zero bytes
    size = 0xDEAD;
    PRINT("try receive of zero bytes on an empty pipe", sceKernelTryReceiveMsgPipe(pipe, buf, 0, MSG_PIPE_MODE_FULL, &size));
    PRINT("try receive of zero bytes receives nothing", size);

    // ASAP takes what there is, FULL needs all of it, and DONT_REMOVE leaves the data in the pipe
    unsigned int got = 0;
    sceKernelSendMsgPipe(pipe, "xyz", 3, MSG_PIPE_MODE_FULL, NULL, NULL);
    PRINT("try receive FULL of more than there is", sceKernelTryReceiveMsgPipe(pipe, buf, 8, MSG_PIPE_MODE_FULL, &size));
    PRINT("receive DONT_REMOVE", sceKernelReceiveMsgPipe(pipe, buf, 8, MSG_PIPE_MODE_ASAP | MSG_PIPE_MODE_DONT_REMOVE, &got, NULL));
    PRINT("receive DONT_REMOVE gets the data", got);
    got = 0;
    PRINT("receive ASAP of more than there is", sceKernelReceiveMsgPipe(pipe, buf, 8, MSG_PIPE_MODE_ASAP, &got, NULL));
    PRINT("receive ASAP gets what there is", got);
    PRINT("the data is still there after DONT_REMOVE", buf[0] == 'x' && buf[2] == 'z');
    PRINT("receive ASAP removed it", sceKernelTryReceiveMsgPipe(pipe, buf, 1, MSG_PIPE_MODE_ASAP, &size));

    // A waiting receive ends when enough data arrives
    receive_result = -1;
    const SceUID receiver = test_start_thread(receive_thread);
    test_wait_until_waiting(receiver);
    sceKernelSendMsgPipe(pipe, "abcd", 4, MSG_PIPE_MODE_FULL, NULL, NULL);
    test_join_thread(receiver);
    PRINT("a waiting receive gets the data sent", receive_result);
    PRINT("the data received is the data sent", received[0] == 'a' && received[3] == 'd');

    // Canceling ends the wait
    int waiting_senders = -1, waiting_receivers = -1;
    receive_result = -1;
    const SceUID canceled = test_start_thread(receive_thread);
    test_wait_until_waiting(canceled);
    PRINT("cancel", sceKernelCancelMsgPipe(pipe, &waiting_senders, &waiting_receivers));
    test_join_thread(canceled);
    PRINT("cancel counts the waiting senders", waiting_senders);
    PRINT("cancel counts the waiting receivers", waiting_receivers);
    PRINT("a canceled receive", receive_result);

    // Deleting ends the wait
    receive_result = -1;
    const SceUID deleted = test_start_thread(receive_thread);
    test_wait_until_waiting(deleted);
    PRINT("delete", sceKernelDeleteMsgPipe(pipe));
    test_join_thread(deleted);
    PRINT("a receive on a deleted pipe", receive_result);

    test_exit();
    return 0;
}
