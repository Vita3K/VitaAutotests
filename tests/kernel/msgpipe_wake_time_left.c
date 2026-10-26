// The time left that a message pipe call writes into a waiting thread's timeout variable when it
// ends the wait: a wake by the other side, a cancel, or a timeout, which writes 0. A partial
// transfer ends nothing, so the variable keeps the value it was given. Every waiter gets a timeout
// of 6 seconds, and the one that times out gets 2 seconds. Each waiter has its own timeout variable
// in a global, which the main thread reads. The main thread is pinned to core 0 at 0x60 and the
// waiters to core 0 at 0xB0. The time left is printed as two comparisons: written means it changed,
// is below the timeout the waiter was given and is above half of it, and unchanged means it still
// holds that timeout. The time left of each waiter that a call ended or partly served is read right
// after that call, before anything blocks, which shows what the call wrote before the waiter ran.
// It is read again once the waiter has ended, since the variable can change while the waiter is
// still returning from its call. A waiter that stays in its wait is read right after the call that
// left it waiting. The cases:
//
// - a FULL receive of 4 is woken by a FULL send of "abcd".
// - a FULL sender of "pqrs" waits on a full pipe, and a FULL receive of 4 makes room for it.
// - a FULL receive of 8 gets 4 bytes from a send, which leaves its time left unchanged, and it
//   still waits. A send of 4 more bytes then ends it.
// - three receivers wait in order, FULL 2, ASAP 4 and FULL 8. A FULL send of "abcdefgh" serves the
//   first two and gives the third "gh", which leaves its time left unchanged. A cancel then ends
//   the third.
// - a FULL receive of 8 gets 4 bytes from a send and times out, which writes 0 to the time left and
//   ends the wait with 0x80028005 and the 4 bytes.
//
// Every case uses a fresh pipe, and cancels or deletes it at the end.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// The pipe holds one 4 KiB page
#define CAPACITY 4096
#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define WAITERS 3
// The timeout of a waiter that is woken, far past what the main thread needs to wake it
#define TIMEOUT (3 * TEST_WAIT_US)
// The timeout of the waiter that ends by timeout
#define SHORT_TIMEOUT TEST_WAIT_US
#define NOT_SET 0xDEAD
// The byte a receive buffer holds until something writes it
#define MARKER '-'

// One waiting thread: what it does, the out values its pipe call writes, and its receive buffer
typedef struct Waiter {
    SceUID thread;
    int send;
    SceUInt32 mode;
    int size;
    const char *data;
    // The time left, which the pipe call writes when it ends the wait
    SceUInt32 timeout;
    // The timeout the waiter started with
    SceUInt32 given;
    int result;
    SceSize moved;
    char buffer[16];
} Waiter;

// What the main thread reads from the pipe and from one waiter, without a call that blocks
typedef struct Seen {
    int free_size;
    int senders;
    int receivers;
    int waiting;
    SceSize moved;
    char buffer[16];
} Seen;

static SceUID pipe;
static Waiter waiters[WAITERS];
static char fill[CAPACITY], drained[CAPACITY];

// The type of the wait thread is in, without the callback bit, or 0 if it doesn't wait
static SceUInt32 wait_type(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    if (sceKernelGetThreadInfo(thread, &info) < 0 || !(info.status & SCE_THREAD_WAITING))
        return 0;
    return info.waitType & ~WAIT_TYPE_CB;
}

// Whether a time left was written: it changed, it is below the timeout given and above half of it
static int time_written(const Waiter *w, SceUInt32 left) {
    return left != w->given && left < w->given && left > w->given / 2;
}

// Runs the send or receive of the waiter whose index is in argp
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    Waiter *w = &waiters[*(const int *)argp];
    if (w->send)
        w->result = sceKernelSendMsgPipe(pipe, (void *)w->data, w->size, w->mode, &w->moved, &w->timeout);
    else
        w->result = sceKernelReceiveMsgPipe(pipe, w->buffer, w->size, w->mode, &w->moved, &w->timeout);
    return 0;
}

// Starts waiter i with a send or a receive, then waits until it waits on the pipe
static void start_waiter(int i, int send, SceUInt32 mode, int size, const char *data, SceUInt32 timeout) {
    Waiter *w = &waiters[i];
    w->send = send;
    w->mode = mode;
    w->size = size;
    w->data = data;
    w->timeout = timeout;
    w->given = timeout;
    w->result = NOT_SET;
    w->moved = NOT_SET;
    sceClibMemset(w->buffer, 0, sizeof(w->buffer));
    sceClibMemset(w->buffer, MARKER, 8);
    w->thread = sceKernelCreateThread("waiter", waiter_entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w->thread, sizeof(i), &i);
    TEST_WAIT_FOR(wait_type(w->thread) == WAIT_TYPE_MSG_PIPE);
}

// Reads the pipe and waiter i without a call that blocks
static Seen look(int i) {
    Seen seen = { 0 };
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    const Waiter *w = &waiters[i];
    seen.free_size = info.freeSize;
    seen.senders = info.numSendWaitThreads;
    seen.receivers = info.numReceiveWaitThreads;
    seen.waiting = wait_type(w->thread) == WAIT_TYPE_MSG_PIPE;
    seen.moved = w->moved;
    sceClibMemcpy(seen.buffer, w->buffer, sizeof(seen.buffer) - 1);
    return seen;
}

// Prints the pipe's state as read after the call named by when
static void print_pipe(const char *name, const char *when, const Seen *seen) {
    PRINTF(seen->free_size, "%s, %s: the pipe's free size", name, when);
    PRINTF(seen->senders, "%s, %s: the waiting senders", name, when);
    PRINTF(seen->receivers, "%s, %s: the waiting receivers", name, when);
}

// Prints one waiter's state as read after the call named by when, with its buffer if show_buffer
static void print_waiter(const char *name, const char *when, const Seen *seen, int show_buffer) {
    PRINTF(seen->waiting, "%s, %s: the waiter still waits", name, when);
    PRINTF(seen->moved, "%s, %s: the waiter's size", name, when);
    if (show_buffer)
        PRINTF(sceClibStrnlen(seen->buffer, 16), "%s, %s: the waiter's buffer \"%s\"", name, when, seen->buffer);
}

// Prints the time left that was read from waiter w, after the call named by when
static void print_left(const char *name, const char *when, const Waiter *w, SceUInt32 left) {
    PRINTF(time_written(w, left), "%s, %s: the time left is written", name, when);
    PRINTF(left == w->given, "%s, %s: the time left is unchanged", name, when);
}

// Prints what the waiter reports once the main thread joined it
static void print_after(const char *name, int i, int show_buffer) {
    const Waiter *w = &waiters[i];
    PRINTF(w->result, "%s: after the waiter ran: its result", name);
    PRINTF(w->moved, "%s: after the waiter ran: its size", name);
    if (show_buffer)
        PRINTF(sceClibStrnlen(w->buffer, 16), "%s: after the waiter ran: its buffer \"%s\"", name, w->buffer);
}

// A FULL receive of 4 waits, then a FULL send of "abcd" wakes it
static void receiver_case(const char *name) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    start_waiter(0, 0, MSG_PIPE_MODE_FULL, 4, NULL, TIMEOUT);
    const Seen before = look(0);
    SceUInt32 timeout = TEST_WAIT_US;
    SceSize sent = NOT_SET;
    const int wake = sceKernelSendMsgPipe(pipe, "abcd", 4, MSG_PIPE_MODE_FULL, &sent, &timeout);
    const SceUInt32 at_once = waiters[0].timeout;
    const Seen seen = look(0);
    test_join_thread(waiters[0].thread);
    const SceUInt32 left = waiters[0].timeout;
    PRINTF(before.waiting, "%s: the waiter waits before the send", name);
    PRINTF(wake, "%s: the send", name);
    PRINTF(sent, "%s: the send's size", name);
    print_pipe(name, "right after the send", &seen);
    print_waiter(name, "right after the send", &seen, 1);
    print_left(name, "right after the wake", &waiters[0], at_once);
    print_left(name, "once the waiter ended", &waiters[0], left);
    print_after(name, 0, 1);
    sceKernelDeleteMsgPipe(pipe);
}

// A FULL sender of "pqrs" waits on a full pipe, then a FULL receive of 4 makes room and wakes it
static void sender_case(const char *name) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    SceUInt32 timeout = TEST_WAIT_US;
    SceSize got = NOT_SET;
    const int filled = sceKernelSendMsgPipe(pipe, fill, CAPACITY, MSG_PIPE_MODE_FULL, &got, &timeout);
    start_waiter(0, 1, MSG_PIPE_MODE_FULL, 4, "pqrs", TIMEOUT);
    const Seen before = look(0);
    timeout = TEST_WAIT_US;
    got = NOT_SET;
    const int wake = sceKernelReceiveMsgPipe(pipe, drained, 4, MSG_PIPE_MODE_FULL, &got, &timeout);
    const SceUInt32 at_once = waiters[0].timeout;
    const SceSize received = got;
    const Seen seen = look(0);
    test_join_thread(waiters[0].thread);
    const SceUInt32 left = waiters[0].timeout;
    PRINTF(filled, "%s: the fill of 4096", name);
    PRINTF(before.waiting, "%s: the sender waits before the receive", name);
    PRINTF(before.free_size, "%s: while the sender waits: the pipe's free size", name);
    PRINTF(wake, "%s: the receive", name);
    PRINTF(received, "%s: the receive's size", name);
    print_pipe(name, "right after the receive", &seen);
    print_waiter(name, "right after the receive", &seen, 0);
    print_left(name, "right after the wake", &waiters[0], at_once);
    print_left(name, "once the waiter ended", &waiters[0], left);
    print_after(name, 0, 0);

    // The sender's bytes went into the ring behind the 4092 dots left, so a drain ends with them
    timeout = TEST_WAIT_US;
    got = NOT_SET;
    PRINTF(sceKernelReceiveMsgPipe(pipe, drained, CAPACITY, MSG_PIPE_MODE_FULL, &got, &timeout), "%s: the drain", name);
    PRINTF(got, "%s: the drain's size", name);
    PRINTF(sceClibMemcmp(drained + CAPACITY - 4, "pqrs", 4) == 0, "%s: the drain ends with \"pqrs\"", name);
    sceKernelDeleteMsgPipe(pipe);
}

// A FULL receive of 8 gets 4 bytes from a FULL send, then 4 more from a second send
static void partial_case(const char *name) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    start_waiter(0, 0, MSG_PIPE_MODE_FULL, 8, NULL, TIMEOUT);
    SceUInt32 timeout = TEST_WAIT_US;
    SceSize sent = NOT_SET;
    const int first = sceKernelSendMsgPipe(pipe, "abcd", 4, MSG_PIPE_MODE_FULL, &sent, &timeout);
    const SceUInt32 first_left = waiters[0].timeout;
    const SceSize first_size = sent;
    const Seen partial = look(0);
    sent = NOT_SET;
    const int second = sceKernelSendMsgPipe(pipe, "efgh", 4, MSG_PIPE_MODE_FULL, &sent, &timeout);
    const SceUInt32 second_at_once = waiters[0].timeout;
    const SceSize second_size = sent;
    const Seen seen = look(0);
    test_join_thread(waiters[0].thread);
    const SceUInt32 second_left = waiters[0].timeout;
    PRINTF(first, "%s: the first send", name);
    PRINTF(first_size, "%s: the first send's size", name);
    print_pipe(name, "after the first send", &partial);
    print_waiter(name, "after the first send", &partial, 1);
    print_left(name, "after the first send", &waiters[0], first_left);
    print_left(name, "right after the wake", &waiters[0], first_left);
    PRINTF(second, "%s: the second send", name);
    PRINTF(second_size, "%s: the second send's size", name);
    print_pipe(name, "after the second send", &seen);
    print_waiter(name, "after the second send", &seen, 1);
    print_left(name, "right after the wake", &waiters[0], second_at_once);
    print_left(name, "once the waiter ended", &waiters[0], second_left);
    print_after(name, 0, 1);
    sceKernelDeleteMsgPipe(pipe);
}

// Three receivers wait in order, FULL 2, ASAP 4 and FULL 8. A FULL send of 8 serves the first two,
// then a cancel ends the third.
static void three_case(const char *name) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    start_waiter(0, 0, MSG_PIPE_MODE_FULL, 2, NULL, TIMEOUT);
    start_waiter(1, 0, MSG_PIPE_MODE_ASAP, 4, NULL, TIMEOUT);
    start_waiter(2, 0, MSG_PIPE_MODE_FULL, 8, NULL, TIMEOUT);
    SceUInt32 timeout = TEST_WAIT_US;
    SceSize sent = NOT_SET;
    const int wake = sceKernelSendMsgPipe(pipe, "abcdefgh", 8, MSG_PIPE_MODE_FULL, &sent, &timeout);
    const SceUInt32 first_at_once = waiters[0].timeout;
    const SceUInt32 second_at_once = waiters[1].timeout;
    const SceUInt32 third_left = waiters[2].timeout;
    const Seen first = look(0);
    const Seen second = look(1);
    const Seen third = look(2);
    test_join_thread(waiters[0].thread);
    test_join_thread(waiters[1].thread);
    const SceUInt32 first_left = waiters[0].timeout;
    const SceUInt32 second_left = waiters[1].timeout;
    PRINTF(wake, "%s: the send", name);
    PRINTF(sent, "%s: the send's size", name);
    print_pipe(name, "right after the send", &third);
    print_waiter("receiver 1, FULL 2", "right after the send", &first, 1);
    print_left("receiver 1, FULL 2", "right after the wake", &waiters[0], first_at_once);
    print_left("receiver 1, FULL 2", "once the waiter ended", &waiters[0], first_left);
    print_waiter("receiver 2, ASAP 4", "right after the send", &second, 1);
    print_left("receiver 2, ASAP 4", "right after the wake", &waiters[1], second_at_once);
    print_left("receiver 2, ASAP 4", "once the waiter ended", &waiters[1], second_left);
    print_waiter("receiver 3, FULL 8", "right after the send", &third, 1);
    print_left("receiver 3, FULL 8", "right after the send", &waiters[2], third_left);
    print_left("receiver 3, FULL 8", "right after the wake", &waiters[2], third_left);
    print_after("receiver 1, FULL 2", 0, 1);
    print_after("receiver 2, ASAP 4", 1, 1);

    int senders = NOT_SET, receivers = NOT_SET;
    const int canceled = sceKernelCancelMsgPipe(pipe, &senders, &receivers);
    const SceUInt32 canceled_at_once = waiters[2].timeout;
    test_join_thread(waiters[2].thread);
    const SceUInt32 canceled_left = waiters[2].timeout;
    PRINTF(canceled, "%s: the cancel", name);
    PRINTF(senders, "%s: the cancel's senders", name);
    PRINTF(receivers, "%s: the cancel's receivers", name);
    print_left("receiver 3, FULL 8", "right after the wake", &waiters[2], canceled_at_once);
    print_left("receiver 3, FULL 8", "once the cancel ended the wait", &waiters[2], canceled_left);
    print_after("receiver 3, FULL 8", 2, 1);
    sceKernelDeleteMsgPipe(pipe);
}

// A FULL receive of 8 gets 4 bytes from a send, then times out with a shorter timeout
static void timeout_case(const char *name) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    start_waiter(0, 0, MSG_PIPE_MODE_FULL, 8, NULL, SHORT_TIMEOUT);
    SceUInt32 timeout = TEST_WAIT_US;
    SceSize sent = NOT_SET;
    const int wake = sceKernelSendMsgPipe(pipe, "abcd", 4, MSG_PIPE_MODE_FULL, &sent, &timeout);
    const SceUInt32 partial_left = waiters[0].timeout;
    const Seen partial = look(0);
    test_join_thread(waiters[0].thread);
    const SceUInt32 ended_left = waiters[0].timeout;
    PRINTF(wake, "%s: the send", name);
    PRINTF(sent, "%s: the send's size", name);
    print_waiter(name, "after the send", &partial, 1);
    print_left(name, "after the send", &waiters[0], partial_left);
    print_left(name, "right after the wake", &waiters[0], partial_left);
    print_after(name, 0, 1);
    PRINTF(ended_left == 0, "%s: after the timeout, the time left is 0", name);
    sceKernelDeleteMsgPipe(pipe);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);
    sceClibMemset(fill, '.', sizeof(fill));

    receiver_case("receive FULL 4, send FULL 4");
    sender_case("send FULL 4 on a full pipe, receive 4");
    partial_case("receive FULL 8, send 4 then 4");
    three_case("three receivers, send 8, cancel");
    timeout_case("receive FULL 8, send 4, timeout");

    test_exit();
    return 0;
}
