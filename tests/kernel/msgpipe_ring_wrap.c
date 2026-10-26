// Bytes that wrap around the end of a message pipe's ring, on every path that moves them: a plain
// send and receive, a send that hands bytes to a waiting receiver, the rest of a send that goes
// into the ring, a FULL sender that waits for room, and an ASAP DONT_REMOVE vector receive. Each
// case starts on a fresh pipe, and moves the ring to a known offset by sending that many '.' bytes
// and receiving them again. The bytes wrap when the offset plus their count passes the end of the
// 4096 byte ring. The test prints the results and sizes, whether the bytes are in order, and the
// free size. Cases:
// - offset 4090: a send of 12 bytes into the empty ring, then a FULL receive of 12
// - offset 4090: a FULL receive of 12 that waits, a send of 12 that serves it, then a send and a
//   receive of 6 bytes that continue after the wrapped ones
// - offset 4092: a FULL receive of 2 that waits, then a send of 14 whose 12 bytes left over wrap
//   into the ring, and a FULL receive of those 12 bytes
// - offset 4090: 12 wrapped bytes read by an ASAP DONT_REMOVE vector receive into pieces of 5, 0
//   and 7 bytes, which keeps them in the pipe, then a FULL receive of the same 12 bytes
// - offset 100: a full ring of a letter pattern, a FULL sender of 8 bytes that waits for room, then
//   a FULL receive of the whole ring, which lets the sender's 8 bytes into the pipe. This case is
//   last, since a sender is waiting when its receive runs.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// A Vita rounds a pipe's buffer up to 4 KiB, so the pipe is created with that size
#define CAPACITY 4096
#define PIECE_SIZE 8

typedef struct Op {
    int send;
    SceUInt32 mode;
    SceSize size;
    const char *data;
} Op;

static SceUID pipe;
static Op op;
static SceUID thread;
static volatile int result, done;
static SceSize moved;
// The bytes the wait thread receives, zeroed for each wait
static char got[CAPACITY];
// The '.' bytes that move the ring to its offset, and the bytes a receive of the offset drops
static char dots[CAPACITY], dropped[CAPACITY];
static char pattern[CAPACITY];

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

// Runs op as a plain send or receive, which waits until it ends
static int op_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    if (op.send)
        result = sceKernelSendMsgPipe(pipe, (void *)op.data, op.size, op.mode, &moved, &timeout);
    else
        result = sceKernelReceiveMsgPipe(pipe, got, op.size, op.mode, &moved, &timeout);
    done = 1;
    return 0;
}

// Starts a send or a receive of size bytes on a new thread, and returns once the thread had a
// chance to wait
static void start_op(int send, SceUInt32 mode, SceSize size, const char *data) {
    op = (Op){ send, mode, size, data };
    sceClibMemset(got, 0, sizeof(got));
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

// Creates a pipe whose ring is at offset, by sending that many dots and receiving them again
static void new_pipe(SceSize offset) {
    SceSize size = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    if (offset == 0)
        return;
    sceKernelTrySendMsgPipe(pipe, dots, offset, MSG_PIPE_MODE_FULL, NULL);
    sceKernelReceiveMsgPipe(pipe, dropped, offset, MSG_PIPE_MODE_FULL, &size, &timeout);
}

// Cancels and deletes the pipe, which ends any wait still going
static void end_pipe(void) {
    int senders, receivers;
    sceKernelCancelMsgPipe(pipe, &senders, &receivers);
    sceKernelDeleteMsgPipe(pipe);
}

// Offset 4090: a send of 12 bytes into the empty ring, then a FULL receive of them
static void case_send_then_receive(void) {
    char out[16] = { 0 };
    SceSize size = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;

    new_pipe(4090);
    PRINT("offset 4090, send 12: the send", sceKernelTrySendMsgPipe(pipe, "ABCDEFGHIJKL", 12, MSG_PIPE_MODE_FULL, &size));
    PRINT("offset 4090, send 12: its size", size);
    PRINT("offset 4090, send 12: the free size", pipe_info().freeSize);
    PRINT("offset 4090, FULL receive of 12: the receive", sceKernelReceiveMsgPipe(pipe, out, 12, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINT("offset 4090, FULL receive of 12: its size", size);
    PRINTF(sceClibStrnlen(out, sizeof(out)), "offset 4090, FULL receive of 12: it got \"%s\"", out);
    PRINT("offset 4090, FULL receive of 12: the bytes are in order", sceClibMemcmp(out, "ABCDEFGHIJKL", 12) == 0);
    PRINT("offset 4090, FULL receive of 12: the free size", pipe_info().freeSize);
    end_pipe();
}

// Offset 4090: a FULL receive of 12 that waits, a send of 12 that serves it, and then a send and a
// receive of 6 bytes
static void case_waiting_receive(void) {
    char out[16] = { 0 };
    SceSize size = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;

    new_pipe(4090);
    start_op(0, MSG_PIPE_MODE_FULL, 12, NULL);
    settle();
    PRINT("offset 4090, the waiting FULL receive of 12: it waits", waits_in(thread, WAIT_TYPE_MSG_PIPE));
    PRINT("offset 4090, send 12 serving the receive: the send", sceKernelTrySendMsgPipe(pipe, "ABCDEFGHIJKL", 12, MSG_PIPE_MODE_FULL, &size));
    PRINT("offset 4090, send 12 serving the receive: its size", size);
    settle();
    join_op();
    PRINT("offset 4090, the waiting FULL receive of 12: its result", result);
    PRINT("offset 4090, the waiting FULL receive of 12: its size", moved);
    PRINTF(sceClibStrnlen(got, 16), "offset 4090, the waiting FULL receive of 12: it got \"%s\"", got);
    PRINT("offset 4090, free size after the hand-over", pipe_info().freeSize);
    PRINT("offset 4090 plus 12, send 6: the send", sceKernelTrySendMsgPipe(pipe, "mnopqr", 6, MSG_PIPE_MODE_FULL, &size));
    PRINT("offset 4090 plus 12, FULL receive of 6: the receive", sceKernelReceiveMsgPipe(pipe, out, 6, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINTF(sceClibStrnlen(out, sizeof(out)), "offset 4090 plus 12, FULL receive of 6: it got \"%s\"", out);
    PRINT("offset 4090 plus 12, FULL receive of 6: the free size", pipe_info().freeSize);
    end_pipe();
}

// Offset 4092: a FULL receive of 2 that waits, then a send of 14 whose 12 bytes left over wrap into
// the ring, and a FULL receive of those 12 bytes
static void case_split_send(void) {
    char out[16] = { 0 };
    SceSize size = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;

    new_pipe(4092);
    start_op(0, MSG_PIPE_MODE_FULL, 2, NULL);
    settle();
    PRINT("offset 4092, FULL receive of 2: it waits", waits_in(thread, WAIT_TYPE_MSG_PIPE));
    PRINT("offset 4092, send 14: the send", sceKernelTrySendMsgPipe(pipe, "ABCDEFGHIJKLMN", 14, MSG_PIPE_MODE_FULL, &size));
    PRINT("offset 4092, send 14: its size", size);
    settle();
    join_op();
    PRINT("offset 4092, FULL receive of 2: its result", result);
    PRINTF(sceClibStrnlen(got, 16), "offset 4092, FULL receive of 2: it got \"%s\"", got);
    PRINT("offset 4092, the free size after the send", pipe_info().freeSize);
    PRINT("offset 4092, FULL receive of 12: the receive", sceKernelReceiveMsgPipe(pipe, out, 12, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINT("offset 4092, FULL receive of 12: its size", size);
    PRINTF(sceClibStrnlen(out, sizeof(out)), "offset 4092, FULL receive of 12: it got \"%s\"", out);
    PRINT("offset 4092, FULL receive of 12: the bytes are in order", sceClibMemcmp(out, "CDEFGHIJKLMN", 12) == 0);
    PRINT("offset 4092, FULL receive of 12: the free size", pipe_info().freeSize);
    end_pipe();
}

// Offset 4090: 12 wrapped bytes read by an ASAP DONT_REMOVE vector receive into pieces of 5, 0 and
// 7 bytes, which keeps them in the pipe, then a FULL receive of the same 12 bytes
static void case_peek_wrapped(void) {
    MsgPipeVector pieces[3];
    char p0[PIECE_SIZE] = { 0 }, p1[PIECE_SIZE] = { 0 }, p2[PIECE_SIZE] = { 0 };
    char out[16] = { 0 };
    SceSize size = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;

    new_pipe(4090);
    PRINT("offset 4090, send 12 that wraps: the send", sceKernelTrySendMsgPipe(pipe, "ABCDEFGHIJKL", 12, MSG_PIPE_MODE_FULL, &size));
    PRINT("offset 4090, send 12 that wraps: its size", size);
    pieces[0] = (MsgPipeVector){ p0, 5 };
    pieces[1] = (MsgPipeVector){ p1, 0 };
    pieces[2] = (MsgPipeVector){ p2, 7 };
    PRINT("offset 4090, ASAP DONT_REMOVE vector receive: the receive", sceKernelReceiveMsgPipeVector(pipe, pieces, 3, MSG_PIPE_MODE_ASAP | MSG_PIPE_MODE_DONT_REMOVE, &size, &timeout));
    PRINT("offset 4090, ASAP DONT_REMOVE vector receive: its size", size);
    PRINTF(sceClibStrnlen(p0, PIECE_SIZE), "offset 4090, ASAP DONT_REMOVE vector receive: piece 1 is \"%s\"", p0);
    PRINTF(sceClibStrnlen(p1, PIECE_SIZE), "offset 4090, ASAP DONT_REMOVE vector receive: piece 2 is \"%s\"", p1);
    PRINTF(sceClibStrnlen(p2, PIECE_SIZE), "offset 4090, ASAP DONT_REMOVE vector receive: piece 3 is \"%s\"", p2);
    PRINT("offset 4090, after the peek: the pieces are in order", sceClibMemcmp(p0, "ABCDE", 5) == 0 && sceClibMemcmp(p2, "FGHIJKL", 7) == 0);
    PRINT("offset 4090, after the peek: the free size", pipe_info().freeSize);
    PRINT("offset 4090, after the peek, FULL receive of 12: the receive", sceKernelReceiveMsgPipe(pipe, out, 12, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINT("offset 4090, after the peek, FULL receive of 12: its size", size);
    PRINTF(sceClibStrnlen(out, sizeof(out)), "offset 4090, after the peek, FULL receive of 12: it got \"%s\"", out);
    PRINT("offset 4090, after the peek, FULL receive of 12: the bytes are in order", sceClibMemcmp(out, "ABCDEFGHIJKL", 12) == 0);
    PRINT("offset 4090, after the peek, FULL receive of 12: the free size", pipe_info().freeSize);
    end_pipe();
}

// Offset 100: a full ring of the pattern, a FULL sender of 8 bytes that waits for room, then a FULL
// receive of the whole ring, which lets the sender's 8 bytes into the pipe
static void case_full_ring_sender(void) {
    static char whole[CAPACITY];
    char tail[16] = { 0 };
    SceSize size = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;

    new_pipe(100);
    PRINT("offset 100, try send of 4096 pattern bytes: the send", sceKernelTrySendMsgPipe(pipe, pattern, CAPACITY, MSG_PIPE_MODE_FULL, &size));
    PRINT("offset 100, try send of 4096 pattern bytes: its size", size);
    PRINT("offset 100, the free size with the ring full", pipe_info().freeSize);
    start_op(1, MSG_PIPE_MODE_FULL, 8, "01234567");
    settle();
    PRINT("offset 100, the FULL sender of 8 waits", waits_in(thread, WAIT_TYPE_MSG_PIPE));
    PRINT("offset 100, FULL receive of 4096: the receive", sceKernelReceiveMsgPipe(pipe, whole, CAPACITY, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINT("offset 100, FULL receive of 4096: its size", size);
    PRINT("offset 100, FULL receive of 4096: the bytes are the pattern in order", sceClibMemcmp(whole, pattern, CAPACITY) == 0);
    settle();
    join_op();
    PRINT("offset 100, the sender's result", result);
    PRINT("offset 100, the sender's size", moved);
    PRINT("offset 100, the free size after the receive", pipe_info().freeSize);
    PRINT("offset 100, FULL receive of 8: the receive", sceKernelReceiveMsgPipe(pipe, tail, 8, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINTF(sceClibStrnlen(tail, sizeof(tail)), "offset 100, FULL receive of 8: it got \"%s\"", tail);
    PRINT("offset 100, FULL receive of 8: the free size", pipe_info().freeSize);
    end_pipe();
}

int main(void) {
    sceClibMemset(dots, '.', sizeof(dots));
    for (int i = 0; i < CAPACITY; i++)
        pattern[i] = (char)('A' + i % 26);
    case_send_then_receive();
    case_waiting_receive();
    case_split_send();
    case_peek_wrapped();
    case_full_ring_sender();
    test_exit();
    return 0;
}
