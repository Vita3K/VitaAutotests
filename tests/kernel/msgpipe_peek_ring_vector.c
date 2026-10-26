// Vector receives with DONT_REMOVE on a message pipe that holds bytes, and what the pipe holds for
// the receives after them. A try send puts "abcdef" into an empty pipe with nothing waiting. Then:
// - a DONT_REMOVE ASAP vector receive into three pieces of 2, 0 and 3 bytes, where the middle
//   piece has a valid buffer and no room
// - the same DONT_REMOVE ASAP vector receive into two pieces of 1 and 4 bytes
// - a plain FULL receive of 6 bytes, which takes what the pipe holds
// The test prints each result and size, the bytes each piece got and how many bytes that is, the
// bytes of the final receive, and the pipe's free size after the send, after each vector receive
// and after the final receive. Every buffer is zeroed before its receive, so a byte a call didn't
// write shows as a 0. No thread waits on the pipe, and every call here ends at once.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define NOT_WRITTEN 0xDEAD
#define PIECE_SIZE 8

static SceUID pipe;
static char pieces[3][PIECE_SIZE];

static void clear_pieces(void) {
    sceClibMemset(pieces, 0, sizeof(pieces));
}

static SceKernelMppInfo pipe_info(void) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    return info;
}

// Runs a DONT_REMOVE ASAP vector receive into vec and prints the result, the size and what each of
// the count pieces got
static void peek_pieces(const char *what, const MsgPipeVector *vec, int count) {
    SceSize size = NOT_WRITTEN;
    SceUInt32 timeout = TEST_WAIT_US;
    PRINTF(sceKernelReceiveMsgPipeVector(pipe, vec, count, MSG_PIPE_MODE_ASAP | MSG_PIPE_MODE_DONT_REMOVE, &size, &timeout), "%s: the receive", what);
    PRINTF(size, "%s: its size", what);
    for (int i = 0; i < count; i++)
        PRINTF(sceClibStrnlen(pieces[i], PIECE_SIZE), "%s: piece %d got \"%s\", its size", what, i + 1, pieces[i]);
    PRINTF(pipe_info().freeSize, "%s: the free size after it", what);
}

int main(void) {
    char all[PIECE_SIZE];
    SceSize size = NOT_WRITTEN;
    SceUInt32 timeout = TEST_WAIT_US;

    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    PRINT("try send of \"abcdef\"", sceKernelTrySendMsgPipe(pipe, "abcdef", 6, MSG_PIPE_MODE_FULL, &size));
    PRINT("try send of \"abcdef\", its size", size);
    PRINT("the free size after the send", pipe_info().freeSize);

    // Pieces of 2, 0 and 3 bytes: the middle piece gets nothing, and the pipe keeps its bytes
    clear_pieces();
    MsgPipeVector split_2_0_3[3] = { { pieces[0], 2 }, { pieces[1], 0 }, { pieces[2], 3 } };
    peek_pieces("DONT_REMOVE into pieces of 2, 0 and 3", split_2_0_3, 3);

    // Pieces of 1 and 4 bytes, from the same bytes
    clear_pieces();
    MsgPipeVector split_1_4[2] = { { pieces[0], 1 }, { pieces[1], 4 } };
    peek_pieces("DONT_REMOVE into pieces of 1 and 4", split_1_4, 2);

    // A plain FULL receive of 6 gets all of them
    sceClibMemset(all, 0, sizeof(all));
    size = NOT_WRITTEN;
    PRINT("FULL receive of 6", sceKernelReceiveMsgPipe(pipe, all, 6, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINTF(size, "FULL receive of 6, it got \"%s\", its size", all);
    PRINT("the free size after the FULL receive", pipe_info().freeSize);

    sceKernelDeleteMsgPipe(pipe);
    test_exit();
    return 0;
}
