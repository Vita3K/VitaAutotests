// What the info call of a message pipe reports while senders and an event waiter wait on it, and
// after a cancel. The pipe has attr 0x1, which queues its senders by priority, and a buffer that is
// full. Two senders block in a FULL send of 4 bytes. Then a third thread waits with
// sceKernelWaitEvent for bit 0x8000 on the pipe's UID, with a 2 s timeout. The test prints the
// pipe's attr and its waiting senders and receivers before the senders, with them, with the event
// waiter, and after the cancel. It also prints whether each sender and the event waiter wait on the
// pipe, the result of each one after the cancel, and what the cancel counted. Every thread is
// joined before its values are read, and nothing waits on the pipe after the cancel.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define NOT_WRITTEN 0xDEAD
#define SENDERS 2
#define EVENT_BIT 0x8000

static SceUID pipe;
static char fill[CAPACITY];
static volatile int send_result[SENDERS];
static volatile int event_result;
static SceUInt32 event_pattern;
static SceUInt64 event_user_data;

static int sender(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    send_result[i] = sceKernelSendMsgPipe(pipe, "abcd", 4, MSG_PIPE_MODE_FULL, NULL, &timeout);
    return 0;
}

static int event_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    event_result = sceKernelWaitEvent(pipe, EVENT_BIT, &event_pattern, &event_user_data, &timeout);
    return 0;
}

// Prints the pipe's attr and its waiting senders and receivers, each labeled with what
static void print_info(const char *what) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    PRINTF(info.attr, "%s: the attr", what);
    PRINTF(info.numSendWaitThreads, "%s: the waiting senders", what);
    PRINTF(info.numReceiveWaitThreads, "%s: the waiting receivers", what);
}

int main(void) {
    SceUID senders[SENDERS];
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0x1, CAPACITY, NULL);
    sceKernelTrySendMsgPipe(pipe, fill, CAPACITY, MSG_PIPE_MODE_FULL, NULL);
    print_info("before the senders");

    for (int i = 0; i < SENDERS; i++) {
        send_result[i] = NOT_WRITTEN;
        senders[i] = test_start_thread_arg(sender, i);
        test_wait_until_waiting(senders[i]);
        PRINTF(test_in_wait(senders[i], WAIT_TYPE_MSG_PIPE, pipe), "sender %d waits on the pipe", i + 1);
    }
    print_info("with the senders");

    event_result = NOT_WRITTEN;
    event_pattern = NOT_WRITTEN;
    event_user_data = 0;
    const SceUID waiter = test_start_thread(event_waiter);
    test_wait_until_waiting(waiter);
    PRINT("the event waiter waits on the pipe", test_in_wait(waiter, WAIT_TYPE_EVENT, pipe));
    print_info("with the event waiter");

    int waiting_senders = NOT_WRITTEN, waiting_receivers = NOT_WRITTEN;
    PRINT("cancel", sceKernelCancelMsgPipe(pipe, &waiting_senders, &waiting_receivers));
    PRINT("cancel counts the waiting senders", waiting_senders);
    PRINT("cancel counts the waiting receivers", waiting_receivers);

    for (int i = 0; i < SENDERS; i++)
        test_join_thread(senders[i]);
    test_join_thread(waiter);
    for (int i = 0; i < SENDERS; i++)
        PRINTF(send_result[i], "sender %d's result", i + 1);
    PRINT("the event waiter's result", event_result);
    PRINT("the event waiter's event bits", event_pattern);
    print_info("after the cancel");

    sceKernelDeleteMsgPipe(pipe);
    test_exit();
    return 0;
}
