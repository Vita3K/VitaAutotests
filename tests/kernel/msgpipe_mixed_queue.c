// What the info call of a message pipe reports while event waiters queue with its senders and
// receivers, and which attr bit it shows. The test has three parts on fresh pipes. First an event
// waiter waits alone on an empty pipe, for bit 0x1 with a 2 s timeout, and the info is printed
// while it waits. Then two receivers and an event waiter queue in that order on an attr 0x0 pipe,
// and the info is printed. A send of "ABCDEF" serves both receivers and wakes the event waiter, and
// the test prints what each one got, and the waiter's bits and user data halves. Last, for each
// attr from 0x0 to 0x3, a sender and then a receiver each wait and are cancelled. The attr is
// printed while each one waits and after the sender's cancel. Every thread is joined before its
// values are read, and every pipe is cancelled and deleted at the end of its part.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define NOT_WRITTEN 0xDEAD
#define EVENT_BIT 0x1
#define SLOTS 3

enum Kind {
    OP_SEND,
    OP_RECEIVE,
    OP_EVENT,
};

typedef struct Op {
    int kind;
    SceSize size;
    SceUInt32 mode;
} Op;

static SceUID pipe;
static Op ops[SLOTS];
static SceUID threads[SLOTS];
static volatile int result[SLOTS];
static SceSize moved[SLOTS];
static char buf[SLOTS][16];
static SceUInt32 bits[SLOTS];
static SceUInt64 user_data[SLOTS];
static char fill[CAPACITY], drained[CAPACITY];

// Runs the operation that slot i was set up with, with a 2 s timeout
static int worker(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    if (ops[i].kind == OP_SEND)
        result[i] = sceKernelSendMsgPipe(pipe, buf[i], ops[i].size, ops[i].mode, &moved[i], &timeout);
    else if (ops[i].kind == OP_RECEIVE)
        result[i] = sceKernelReceiveMsgPipe(pipe, buf[i], ops[i].size, ops[i].mode, &moved[i], &timeout);
    else
        result[i] = sceKernelWaitEvent(pipe, EVENT_BIT, &bits[i], &user_data[i], &timeout);
    return 0;
}

// Starts slot i on a thread, and returns once that thread waits
static void start(int i) {
    result[i] = NOT_WRITTEN;
    threads[i] = test_start_thread_arg(worker, i);
    test_wait_until_waiting(threads[i]);
}

// Prints the pipe's attr and its waiting senders and receivers, each labeled with what
static void print_info(const char *what) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    PRINTF(info.attr, "%s: the attr", what);
    PRINTF(info.numSendWaitThreads, "%s: the waiting senders", what);
    PRINTF(info.numReceiveWaitThreads, "%s: the waiting receivers", what);
}

// Cancels the pipe, where no thread is left waiting by now, and deletes it
static void cancel_and_delete(void) {
    int senders = 0, receivers = 0;
    sceKernelCancelMsgPipe(pipe, &senders, &receivers);
    sceKernelDeleteMsgPipe(pipe);
}

// An event waiter alone on an empty pipe. It counts as no sender and no receiver, and times out.
static void event_alone(void) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0x0, CAPACITY, NULL);
    ops[0] = (Op){ OP_EVENT, 0, 0 };
    start(0);
    PRINT("the event waiter waits on the pipe", test_in_wait(threads[0], WAIT_TYPE_EVENT, pipe));
    print_info("the event waiter alone");
    test_join_thread(threads[0]);
    PRINT("the event waiter alone: its result", result[0]);
    cancel_and_delete();
}

// Two receivers and an event waiter queue in that order on an attr 0x0 pipe, then a send serves
// both receivers and wakes the event waiter
static void receivers_and_event(void) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0x0, CAPACITY, NULL);
    ops[0] = (Op){ OP_RECEIVE, 4, MSG_PIPE_MODE_FULL };
    ops[1] = (Op){ OP_EVENT, 0, 0 };
    ops[2] = (Op){ OP_RECEIVE, 4, MSG_PIPE_MODE_ASAP };
    for (int i = 0; i < SLOTS; i++) {
        sceClibMemset(buf[i], 0, sizeof(buf[i]));
        bits[i] = NOT_WRITTEN;
        user_data[i] = 0;
        start(i);
    }
    PRINT("the event waiter waits on the pipe", test_in_wait(threads[1], WAIT_TYPE_EVENT, pipe));
    print_info("three waiters");

    SceSize size = NOT_WRITTEN;
    SceUInt32 timeout = TEST_WAIT_US;
    PRINT("the send", sceKernelSendMsgPipe(pipe, "ABCDEF", 6, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINT("the send's size", size);
    for (int i = 0; i < SLOTS; i++)
        test_join_thread(threads[i]);
    print_info("after the send");

    PRINTF(moved[0], "receiver 1, FULL 4, gets \"%s\", size", buf[0]);
    PRINT("receiver 1's result", result[0]);
    PRINT("the event waiter's result", result[1]);
    PRINT("the event waiter's event bits", bits[1]);
    PRINT("the event waiter's user data, high half", (SceUInt32)(user_data[1] >> 32));
    PRINT("the event waiter's user data, low half", (SceUInt32)user_data[1]);
    PRINTF(moved[2], "receiver 2, ASAP 4, gets \"%s\", size", buf[2]);
    PRINT("receiver 2's result", result[2]);
    cancel_and_delete();
}

// A sender waits on a full pipe and is cancelled, then a receiver waits on the emptied pipe. The
// attr is printed while each waits and after the sender's cancel.
static void attr_bits(SceUInt32 attr) {
    char what[96];
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, attr, CAPACITY, NULL);
    sceKernelTrySendMsgPipe(pipe, fill, CAPACITY, MSG_PIPE_MODE_FULL, NULL);

    buf[0][0] = 'z';
    ops[0] = (Op){ OP_SEND, 1, MSG_PIPE_MODE_FULL };
    start(0);
    sceClibSnprintf(what, sizeof(what), "attr 0x%X, a sender waits", attr);
    print_info(what);
    int senders = 0, receivers = 0;
    sceKernelCancelMsgPipe(pipe, &senders, &receivers);
    test_join_thread(threads[0]);
    PRINTF(result[0], "attr 0x%X, the canceled sender's result", attr);
    sceClibSnprintf(what, sizeof(what), "attr 0x%X, after the sender's cancel", attr);
    print_info(what);

    sceKernelTryReceiveMsgPipe(pipe, drained, CAPACITY, MSG_PIPE_MODE_FULL, NULL);
    ops[0] = (Op){ OP_RECEIVE, 1, MSG_PIPE_MODE_FULL };
    start(0);
    sceClibSnprintf(what, sizeof(what), "attr 0x%X, a receiver waits", attr);
    print_info(what);
    sceKernelCancelMsgPipe(pipe, &senders, &receivers);
    test_join_thread(threads[0]);
    PRINTF(result[0], "attr 0x%X, the canceled receiver's result", attr);
    cancel_and_delete();
}

int main(void) {
    sceClibMemset(fill, '.', sizeof(fill));
    event_alone();
    receivers_and_event();
    static const SceUInt32 attrs[] = { 0x0, 0x1, 0x2, 0x3 };
    for (unsigned int a = 0; a < sizeof(attrs) / sizeof(attrs[0]); a++)
        attr_bits(attrs[a]);
    test_exit();
    return 0;
}
