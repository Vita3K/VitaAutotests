// Vector receives and sends that wait on a message pipe, and the pieces they move as several
// hand-overs come in. A waiting vector call keeps its place in its pieces between the hand-overs,
// so a piece of 0 bytes in the middle is passed over and the bytes land in the right piece. The
// sender's bytes are read when they move, and the pieces list is read again on every move. The test
// prints whether each waiting call still waits, its size and the bytes of each piece, and checks
// the bytes that end up in the pipe. Each case uses a fresh pipe, which is cancelled and deleted at
// its end. Cases:
// - a FULL vector receive of pieces 3, 0, 2 and 3 bytes, with four blocking sends "ABC", "DE",
//   "FG" and "H". After each send the test prints whether the receive waits, its size and the four
//   pieces. The pieces end as "ABC", "", "DE" and "FGH".
// - a FULL vector send of pieces 2, 0 and 5 bytes that waits on a pipe holding 4094 bytes. A FULL
//   vector receive of pieces 4094 and 2 bytes takes the 4094 bytes and the first 2 bytes of the
//   sender. The sender's last 5 bytes go into the pipe, and the test prints the sender's size.
// - a FULL vector send of one piece of 4 bytes that waits on a full pipe. The test overwrites the
//   piece's buffer with "WXYZ" before it makes room, so "WXYZ" is what arrives.
// - a FULL vector receive of pieces 4 and 4 bytes that gets "ABCD" into its first piece. The test
//   then points the second piece at another buffer and sends "EFGH", and prints which buffer got
//   the bytes. This case is last, since the outcome isn't known beforehand.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// A Vita rounds a pipe's buffer up to 4 KiB, so the pipe is created with that size
#define CAPACITY 4096
#define PIECE_SIZE 8

// The vector call the wait thread makes
typedef struct Op {
    int send;
    SceUInt32 mode;
    MsgPipeVector *pieces;
    SceUInt32 count;
} Op;

typedef struct Send {
    const char *data;
    SceSize size;
} Send;

static SceUID pipe;
static Op op;
static SceUID thread;
static volatile int result, done;
static SceSize moved;
// The buffers of the pieces the receive cases use, zeroed for each new pipe
static char piece[4][PIECE_SIZE];
static char fill[CAPACITY], drained[CAPACITY];
// The bytes other than '.' the test got back from the pipe
static char seen[64];
static int seen_len;

// The type of the wait thread is in, without the callback bit, or 0 if it doesn't wait
static SceUInt32 wait_type(SceUID id) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    if (sceKernelGetThreadInfo(id, &info) < 0 || !(info.status & SCE_THREAD_WAITING))
        return 0;
    return info.waitType & ~WAIT_TYPE_CB;
}

static int waits_in(SceUID id, SceUInt32 type) {
    return wait_type(id) == type;
}

static SceKernelMppInfo pipe_info(void) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    return info;
}

// Runs op as a vector send or receive, which waits until it ends
static int op_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    if (op.send)
        result = sceKernelSendMsgPipeVector(pipe, op.pieces, op.count, op.mode, &moved, &timeout);
    else
        result = sceKernelReceiveMsgPipeVector(pipe, op.pieces, op.count, op.mode, &moved, &timeout);
    done = 1;
    return 0;
}

// Starts op on a new thread, and returns once the thread had a chance to wait
static void start_op(int send, SceUInt32 mode, MsgPipeVector *pieces, SceUInt32 count) {
    op = (Op){ send, mode, pieces, count };
    result = 0xDEAD;
    done = 0;
    moved = 0xDEAD;
    thread = test_start_thread(op_thread);
    test_wait_until_waiting(thread);
}

static int settled(void) {
    return done || waits_in(thread, WAIT_TYPE_MSG_PIPE);
}

// Waits until the op is done or waits on the pipe, on 20 polls in a row, at most 5 s. A thread that
// was just woken may show its wait until it runs, so one look isn't enough.
static void settle(void) {
    int stable = 0;
    for (int poll = 0; poll < 5000 && stable < 20; poll++) {
        stable = settled() ? stable + 1 : 0;
        if (stable < 20)
            sceKernelDelayThread(1000);
    }
}

// Joins the op thread, which has ended once its call returned or timed out
static void join_op(void) {
    test_join_thread(thread);
}

// Creates an empty pipe, and zeroes the pieces the receive cases use
static void new_pipe(void) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    sceClibMemset(piece, 0, sizeof(piece));
    seen_len = 0;
    seen[0] = '\0';
}

// Cancels and deletes the pipe, which ends any wait still going
static void end_pipe(void) {
    int senders, receivers;
    sceKernelCancelMsgPipe(pipe, &senders, &receivers);
    sceKernelDeleteMsgPipe(pipe);
}

// Receives what the pipe holds without waiting, and adds the bytes other than '.' to seen
static void drain(void) {
    SceSize size = 0;
    if (sceKernelTryReceiveMsgPipe(pipe, drained, CAPACITY, MSG_PIPE_MODE_ASAP, &size) < 0)
        return;
    for (SceSize i = 0; i < size; i++) {
        if (drained[i] != '.' && seen_len < (int)sizeof(seen) - 1)
            seen[seen_len++] = drained[i];
    }
    seen[seen_len] = '\0';
}

// Returns how many of the size bytes at buf are '.'
static int count_dots(const char *buf, SceSize size) {
    int dots = 0;
    for (SceSize i = 0; i < size; i++) {
        if (buf[i] == '.')
            dots++;
    }
    return dots;
}

// A FULL vector receive of pieces 3, 0, 2 and 3 bytes, with four blocking sends
static void case_pieces(void) {
    static const Send sends[] = { { "ABC", 3 }, { "DE", 2 }, { "FG", 2 }, { "H", 1 } };
    static MsgPipeVector pieces[4];
    SceSize size = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;

    new_pipe();
    pieces[0] = (MsgPipeVector){ piece[0], 3 };
    pieces[1] = (MsgPipeVector){ piece[1], 0 };
    pieces[2] = (MsgPipeVector){ piece[2], 2 };
    pieces[3] = (MsgPipeVector){ piece[3], 3 };
    start_op(0, MSG_PIPE_MODE_FULL, pieces, 4);
    for (int s = 0; s < 4; s++) {
        timeout = TEST_WAIT_US;
        PRINTF(sceKernelSendMsgPipe(pipe, (void *)sends[s].data, sends[s].size, MSG_PIPE_MODE_FULL, &size, &timeout), "pieces 3 0 2 3, send \"%s\": the send", sends[s].data);
        settle();
        PRINTF(waits_in(thread, WAIT_TYPE_MSG_PIPE), "pieces 3 0 2 3, after \"%s\": the receive waits", sends[s].data);
        PRINTF(moved, "pieces 3 0 2 3, after \"%s\": its size", sends[s].data);
        for (int i = 0; i < 4; i++)
            PRINTF(sceClibStrnlen(piece[i], PIECE_SIZE), "pieces 3 0 2 3, after \"%s\": piece %d is \"%s\"", sends[s].data, i + 1, piece[i]);
    }
    settle();
    join_op();
    PRINT("pieces 3 0 2 3: the receive's result", result);
    end_pipe();
}

// A FULL vector send of pieces 2, 0 and 5 bytes waits on a pipe that holds 4094 bytes. A FULL
// vector receive of pieces 4094 and 2 bytes takes the 4094 bytes and the first 2 bytes of the
// sender. The sender's last 5 bytes go into the pipe.
static void case_sender_pieces(void) {
    static char ab[PIECE_SIZE] = "ab";
    static char cdefg[PIECE_SIZE] = "cdefg";
    static char big[CAPACITY], tail[PIECE_SIZE];
    static MsgPipeVector sender[3];
    static MsgPipeVector recv[2];
    SceSize size = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;

    new_pipe();
    PRINT("pieces 2 0 5: the try send of 4094 bytes", sceKernelTrySendMsgPipe(pipe, fill, 4094, MSG_PIPE_MODE_FULL, NULL));
    sender[0] = (MsgPipeVector){ ab, 2 };
    sender[1] = (MsgPipeVector){ ab, 0 };
    sender[2] = (MsgPipeVector){ cdefg, 5 };
    start_op(1, MSG_PIPE_MODE_FULL, sender, 3);
    settle();
    PRINT("pieces 2 0 5: the sender waits", waits_in(thread, WAIT_TYPE_MSG_PIPE));
    PRINT("pieces 2 0 5: the free size before the receive", pipe_info().freeSize);
    recv[0] = (MsgPipeVector){ big, 4094 };
    recv[1] = (MsgPipeVector){ tail, 2 };
    PRINT("pieces 2 0 5: the FULL vector receive of 4094 and 2", sceKernelReceiveMsgPipeVector(pipe, recv, 2, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINT("pieces 2 0 5: its size", size);
    PRINT("pieces 2 0 5: the first piece is all dots", count_dots(big, 4094) == 4094);
    PRINTF(sceClibStrnlen(tail, PIECE_SIZE), "pieces 2 0 5: the second piece is \"%s\"", tail);
    settle();
    join_op();
    PRINT("pieces 2 0 5: the sender's result", result);
    PRINT("pieces 2 0 5: the sender's size", moved);
    drain();
    PRINTF(seen_len, "pieces 2 0 5: the pipe holds \"%s\"", seen);
    end_pipe();
}

// A FULL vector send of one piece of 4 bytes waits on a full pipe. The test overwrites the piece's
// buffer before it makes room, and the bytes that arrive are the overwritten ones.
static void case_overwrite(void) {
    static char wxyz[PIECE_SIZE] = "wxyz";
    static MsgPipeVector sender[1];
    SceSize size = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;

    new_pipe();
    PRINT("overwrite: the try send that fills the pipe", sceKernelTrySendMsgPipe(pipe, fill, CAPACITY, MSG_PIPE_MODE_FULL, NULL));
    sender[0] = (MsgPipeVector){ wxyz, 4 };
    start_op(1, MSG_PIPE_MODE_FULL, sender, 1);
    settle();
    PRINT("overwrite: the sender waits on the full pipe", waits_in(thread, WAIT_TYPE_MSG_PIPE));
    sceClibMemcpy(wxyz, "WXYZ", 4);
    PRINT("overwrite: the FULL receive of 4 that makes room", sceKernelReceiveMsgPipe(pipe, drained, 4, MSG_PIPE_MODE_FULL, &size, &timeout));
    settle();
    join_op();
    PRINT("overwrite: the sender's result", result);
    PRINT("overwrite: the sender's size", moved);
    drain();
    PRINTF(seen_len, "overwrite: the pipe gives back \"%s\"", seen);
    end_pipe();
}

// A FULL vector receive of pieces 4 and 4 bytes gets "ABCD" into its first piece. The second piece
// then points at another buffer, and "EFGH" is sent. The test prints which buffer got the bytes.
static void case_list_change(void) {
    static char first[PIECE_SIZE], old_second[PIECE_SIZE], new_second[PIECE_SIZE];
    static MsgPipeVector recv[2];
    SceSize size = 0xDEAD;

    new_pipe();
    recv[0] = (MsgPipeVector){ first, 4 };
    recv[1] = (MsgPipeVector){ old_second, 4 };
    start_op(0, MSG_PIPE_MODE_FULL, recv, 2);
    settle();
    PRINT("list change: the try send of \"ABCD\"", sceKernelTrySendMsgPipe(pipe, "ABCD", 4, MSG_PIPE_MODE_FULL, &size));
    settle();
    PRINT("list change: the receive still waits after 4", waits_in(thread, WAIT_TYPE_MSG_PIPE));
    PRINT("list change: its size after 4", moved);
    PRINTF(sceClibStrnlen(first, PIECE_SIZE), "list change: the first piece is \"%s\"", first);
    recv[1] = (MsgPipeVector){ new_second, 4 };
    PRINT("list change: the try send of \"EFGH\"", sceKernelTrySendMsgPipe(pipe, "EFGH", 4, MSG_PIPE_MODE_FULL, &size));
    settle();
    join_op();
    PRINT("list change: the receive's result", result);
    PRINT("list change: its size", moved);
    PRINT("list change: the old second buffer got EFGH", sceClibStrncmp(old_second, "EFGH", 4) == 0);
    PRINT("list change: the new second buffer got EFGH", sceClibStrncmp(new_second, "EFGH", 4) == 0);
    end_pipe();
}

int main(void) {
    sceClibMemset(fill, '.', sizeof(fill));
    case_pieces();
    case_sender_pieces();
    case_overwrite();
    case_list_change();
    test_exit();
    return 0;
}
