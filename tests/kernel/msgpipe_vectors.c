// Message pipe vector sends and receives: how the pieces join and split, the sizes they report, and
// the errors for bad vector lists
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    const SceUID pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 4096, NULL);
    char a[3] = { 'a', 'b', 'c' }, b[2] = { 'd', 'e' }, c[4] = { 'f', 'g', 'h', 'i' };
    MsgPipeVector send[3] = { { a, sizeof(a) }, { b, sizeof(b) }, { c, sizeof(c) } };
    SceSize size = 0xDEAD;
    SceUInt32 timeout = 1000;

    PRINT("send three pieces", sceKernelSendMsgPipeVector(pipe, send, 3, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINT("the size sent", size);
    char x[4] = { 0 }, y[5] = { 0 };
    MsgPipeVector receive[2] = { { x, sizeof(x) }, { y, sizeof(y) } };
    size = 0xDEAD;
    timeout = 1000;
    PRINT("receive into two pieces", sceKernelReceiveMsgPipeVector(pipe, receive, 2, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINT("the size received", size);
    PRINT("the first piece holds the first 4 bytes", sceClibMemcmp(x, "abcd", 4) == 0);
    PRINT("the second piece holds the rest", sceClibMemcmp(y, "efghi", 5) == 0);

    // Pieces of 0 bytes and an empty list
    MsgPipeVector empty_pieces[2] = { { a, 0 }, { b, 0 } };
    size = 0xDEAD;
    timeout = 1000;
    PRINT("send two empty pieces", sceKernelSendMsgPipeVector(pipe, empty_pieces, 2, MSG_PIPE_MODE_ASAP, &size, &timeout));
    PRINT("the size sent", size);
    size = 0xDEAD;
    timeout = 1000;
    PRINT("send a list of 0 pieces", sceKernelSendMsgPipeVector(pipe, send, 0, MSG_PIPE_MODE_ASAP, &size, &timeout));
    PRINT("the size sent", size);

    // ASAP takes what is there
    sceKernelSendMsgPipeVector(pipe, send, 1, MSG_PIPE_MODE_FULL, &size, NULL);
    sceClibMemset(x, 0, sizeof(x));
    sceClibMemset(y, 0, sizeof(y));
    size = 0xDEAD;
    timeout = 1000;
    PRINT("receive ASAP with less in the pipe than asked", sceKernelReceiveMsgPipeVector(pipe, receive, 2, MSG_PIPE_MODE_ASAP, &size, &timeout));
    PRINT("the size received", size);
    PRINT("it fills the first piece first", sceClibMemcmp(x, "abc", 3) == 0 && x[3] == 0);
    size = 0xDEAD;
    timeout = 1000;
    PRINT("receive FULL from an empty pipe", sceKernelReceiveMsgPipeVector(pipe, receive, 2, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINT("the size received", size);

    // Bad lists
    PRINT("send to an unknown UID", sceKernelSendMsgPipeVector(0x12345, send, 3, MSG_PIPE_MODE_ASAP, &size, NULL));
    PRINT("receive from an unknown UID", sceKernelReceiveMsgPipeVector(0x12345, receive, 2, MSG_PIPE_MODE_ASAP, &size, NULL));
    PRINT("send a list of 1000 pieces", sceKernelSendMsgPipeVector(pipe, send, 1000, MSG_PIPE_MODE_ASAP, &size, NULL));
    PRINT("send with mode 0x200", sceKernelSendMsgPipeVector(pipe, send, 1, 0x200, &size, NULL));

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("send a NULL list", sceKernelSendMsgPipeVector(pipe, NULL, 1, MSG_PIPE_MODE_ASAP, &size, NULL));
    MsgPipeVector null_piece[1] = { { NULL, 4 } };
    PRINT("send a piece with a NULL buffer", sceKernelSendMsgPipeVector(pipe, null_piece, 1, MSG_PIPE_MODE_ASAP, &size, NULL));
    PRINT("receive into a NULL list", sceKernelReceiveMsgPipeVector(pipe, NULL, 1, MSG_PIPE_MODE_ASAP, &size, NULL));

    test_exit();
    return 0;
}
