// A DONT_WAIT send that wakes a receiver which waits. A thread waits in a FULL receive of 4 with a
// 2 second timeout on an empty pipe. The main thread then makes a DONT_WAIT FULL send of 4 bytes,
// and the test prints what the send returns and the size it wrote back. It joins the receiver and
// prints its result, its size and the bytes it got, then waits for the send's event the way
// msgpipe_async waits for its events, and prints the event's bits and both halves of its user data.
// msgpipe_modes covers a blocking send that wakes a receiver, and msgpipe_async covers DONT_WAIT
// sends that end at once, so this test covers the DONT_WAIT send that wakes a waiting receiver.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID pipe;
static volatile int recv_result = 0xDEAD;
static SceSize recv_size = 0xDEAD;
static char recv_buf[4];

static int receiver(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    recv_result = sceKernelReceiveMsgPipe(pipe, recv_buf, sizeof(recv_buf), MSG_PIPE_MODE_FULL, &recv_size, &timeout);
    return 0;
}

// Prints whether the pipe reports the event, with the user data split into its two halves
static void print_event(const char *name, SceUInt32 bit) {
    SceUInt32 pattern = 0xDEAD;
    SceUInt64 user_data = 0xDEADDEADDEADDEADull;
    SceUInt32 timeout = 100000;
    PRINTF(sceKernelWaitEvent(pipe, bit, &pattern, &user_data, &timeout), "%s: wait for the event", name);
    PRINTF(pattern, "%s: the event's bits", name);
    PRINTF((int)(user_data & 0xFFFFFFFF), "%s: the low half of its user data", name);
    PRINTF((int)(user_data >> 32), "%s: the high half of its user data", name);
}

int main(void) {
    SceSize send_size = 0xDEAD;

    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 0x1000, NULL);
    const SceUID thread = test_start_thread(receiver);
    test_wait_until_waiting(thread);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_MSG_PIPE, pipe));
    PRINT("the receiver waits on the pipe", test_in_wait(thread, WAIT_TYPE_MSG_PIPE, pipe));

    PRINT("DONT_WAIT FULL send of 4", sceKernelSendMsgPipe(pipe, "wxyz", 4, MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_WAIT, &send_size, NULL));
    PRINT("its size", send_size);

    test_join_thread(thread);
    PRINT("the receiver's result", recv_result);
    PRINT("the receiver's size", recv_size);
    PRINTF(sceClibStrnlen(recv_buf, sizeof(recv_buf)), "the receiver got \"%.4s\"", recv_buf);
    print_event("the send", EVENT_IN);

    sceKernelDeleteMsgPipe(pipe);
    test_exit();
    return 0;
}
