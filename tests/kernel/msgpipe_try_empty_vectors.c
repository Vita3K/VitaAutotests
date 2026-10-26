// Try sends and try receives of lists of empty pieces on an empty message pipe with nothing
// waiting, in ASAP and in FULL mode, and a try send of an empty piece with a NULL buffer. The
// lists have two empty pieces, each with a valid buffer. Each call's result and size are printed,
// with the size preset to 0xDEAD so a size the call didn't write shows. The cases, in order:
// - a try send of two empty pieces, ASAP, then FULL
// - a try receive of two empty pieces, ASAP, then FULL, from the same empty pipe
// - a try send of one empty piece with a NULL buffer, FULL, last, since a NULL buffer goes at the
//   end
// Every call is a try call, so none of them waits for the pipe. msgpipe_vectors has the blocking
// versions of these lists, and msgpipe_edges the plain calls with 0 bytes.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define NOT_WRITTEN 0xDEAD

int main(void) {
    const SceUID pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    char a[4] = { 0 }, b[4] = { 0 };
    MsgPipeVector empty_pieces[2] = { { a, 0 }, { b, 0 } };
    SceSize size;

    size = NOT_WRITTEN;
    PRINT("try send of two empty pieces ASAP", sceKernelTrySendMsgPipeVector(pipe, empty_pieces, 2, MSG_PIPE_MODE_ASAP, &size));
    PRINT("try send of two empty pieces ASAP, its size", size);
    size = NOT_WRITTEN;
    PRINT("try send of two empty pieces FULL", sceKernelTrySendMsgPipeVector(pipe, empty_pieces, 2, MSG_PIPE_MODE_FULL, &size));
    PRINT("try send of two empty pieces FULL, its size", size);

    size = NOT_WRITTEN;
    PRINT("try receive of two empty pieces ASAP", sceKernelTryReceiveMsgPipeVector(pipe, empty_pieces, 2, MSG_PIPE_MODE_ASAP, &size));
    PRINT("try receive of two empty pieces ASAP, its size", size);
    size = NOT_WRITTEN;
    PRINT("try receive of two empty pieces FULL", sceKernelTryReceiveMsgPipeVector(pipe, empty_pieces, 2, MSG_PIPE_MODE_FULL, &size));
    PRINT("try receive of two empty pieces FULL, its size", size);

    MsgPipeVector null_empty_piece[1] = { { NULL, 0 } };
    size = NOT_WRITTEN;
    PRINT("try send of an empty piece with a NULL buffer FULL", sceKernelTrySendMsgPipeVector(pipe, null_empty_piece, 1, MSG_PIPE_MODE_FULL, &size));
    PRINT("try send of an empty piece with a NULL buffer FULL, its size", size);

    sceKernelDeleteMsgPipe(pipe);
    test_exit();
    return 0;
}
