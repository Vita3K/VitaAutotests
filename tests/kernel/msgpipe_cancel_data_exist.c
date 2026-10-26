// What a cancel of a message pipe does to the pipe's event. The cases, each on a fresh pipe of
// 0x1000 bytes:
//
// - the pipe holds 4 bytes from a send, so its bits are 0x10001 with the send's user data. The
//   test cancels it and prints, before and after the cancel, the bits, both halves of the user
//   data and a poll for 0x10000.
// - a thread waits in a receive on the empty pipe, whose bits were cleared, when the cancel comes.
//   The test prints the cancel's result, the receive's result, and the bits and both halves of the
//   user data once the receive returned.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 0x1000
#define MESSAGE_SIZE 4
#define DATA_EXIST 0x10000

static SceUID pipe;
static char buffer[MESSAGE_SIZE];
static volatile int r_ready, r_result = -1;

// Prints the pipe's bits, its user data and a poll for 0x10000
static void print_event(const char *name) {
    EventInfo info = { .size = sizeof(info) };
    SceUInt32 bits = 0xDEAD;
    SceUInt64 user_data = 0;
    if (sceKernelGetEventInfo(pipe, &info) == 0) {
        bits = info.eventPattern;
        user_data = info.userData;
    }
    PRINTF(bits, "%s: the bits", name);
    PRINTF((SceUInt32)user_data, "%s: the low half of the user data", name);
    PRINTF((SceUInt32)(user_data >> 32), "%s: the high half of the user data", name);
    SceUInt32 got = 0;
    SceUInt64 poll_user_data = 0;
    PRINTF(sceKernelPollEvent(pipe, DATA_EXIST, &got, &poll_user_data), "%s: a poll for 0x10000", name);
}

// Receives 4 bytes, FULL, from the empty pipe
static int r_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    r_ready = 1;
    SceUInt32 timeout = TEST_WAIT_US;
    SceSize size = 0;
    r_result = sceKernelReceiveMsgPipe(pipe, buffer, sizeof(buffer), MSG_PIPE_MODE_FULL, &size, &timeout);
    return 0;
}

int main(void) {
    int senders = -1, receivers = -1;

    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    sceKernelClearEvent(pipe, 0);
    sceKernelTrySendMsgPipe(pipe, buffer, sizeof(buffer), MSG_PIPE_MODE_FULL, NULL);
    print_event("before the cancel");
    PRINT("the cancel", sceKernelCancelMsgPipe(pipe, &senders, &receivers));
    print_event("after the cancel");
    sceKernelDeleteMsgPipe(pipe);

    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    sceKernelClearEvent(pipe, 0);
    const SceUID r = test_start_thread(r_entry);
    TEST_WAIT_FOR(r_ready);
    test_wait_until_waiting(r);
    PRINT("a waiting receive: the cancel", sceKernelCancelMsgPipe(pipe, &senders, &receivers));
    test_join_thread(r);
    PRINT("a waiting receive: its result", r_result);
    print_event("a waiting receive: after it returned");
    sceKernelDeleteMsgPipe(pipe);

    test_exit();
    return 0;
}
