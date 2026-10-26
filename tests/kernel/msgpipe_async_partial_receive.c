// Asynchronous message pipe receives that get less than they asked for, and the event each one
// leaves on the pipe. The cases are:
// - an ASAP receive of 8 with DONT_WAIT on a pipe that holds 3 bytes, then a FULL try receive of 8
//   after it.
// - a FULL receive of 8 with DONT_WAIT on an empty pipe, then a blocking FULL send of 5 and one
//   of 3 that give it all its bytes.
// The test prints each call's result and the size it wrote back, then each event's bits and both
// halves of its user data, and whether the bytes received are the bytes sent. Every event is
// waited for as msgpipe_async waits for its events, and each receive's size is read again after
// its event.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID pipe;

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
    char asap_buf[8] = { 0 };
    char full_buf[8] = { 0 };
    char later[8] = { 0 };
    SceSize size;
    SceSize asap_size = 0xDEAD;
    SceSize full_size = 0xDEAD;
    SceUInt32 timeout;

    // Three bytes in the pipe, then an ASAP receive of 8 that doesn't wait
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 0x1000, NULL);
    PRINT("try send of 3 bytes", sceKernelTrySendMsgPipe(pipe, "abc", 3, MSG_PIPE_MODE_FULL, NULL));
    PRINT("DONT_WAIT ASAP receive of 8 with 3 in the pipe", sceKernelReceiveMsgPipe(pipe, asap_buf, sizeof(asap_buf), MSG_PIPE_MODE_ASAP | MSG_PIPE_MODE_DONT_WAIT, &asap_size, NULL));
    PRINT("its size right after the call", asap_size);
    print_event("the ASAP receive", EVENT_OUT);
    PRINT("its size after the event", asap_size);
    PRINT("the bytes it got are the ones sent", sceClibMemcmp(asap_buf, "abc", 3) == 0);
    PRINT("a FULL try receive of 8 after it", sceKernelTryReceiveMsgPipe(pipe, later, sizeof(later), MSG_PIPE_MODE_FULL, NULL));
    sceKernelDeleteMsgPipe(pipe);

    // An empty pipe, then a FULL receive of 8 that doesn't wait, then a blocking FULL send of 5 and
    // one of 3 that complete it
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 0x1000, NULL);
    PRINT("DONT_WAIT FULL receive of 8 from an empty pipe", sceKernelReceiveMsgPipe(pipe, full_buf, sizeof(full_buf), MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_WAIT, &full_size, NULL));
    PRINT("its size right after the call", full_size);
    size = 0xDEAD;
    timeout = TEST_WAIT_US;
    PRINT("blocking FULL send of 5", sceKernelSendMsgPipe(pipe, "vwxyz", 5, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINT("its size", size);
    size = 0xDEAD;
    timeout = TEST_WAIT_US;
    PRINT("blocking FULL send of 3", sceKernelSendMsgPipe(pipe, "123", 3, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINT("its size", size);
    print_event("the FULL receive", EVENT_OUT);
    PRINT("its size after the event", full_size);
    PRINT("the bytes it got are the ones sent", sceClibMemcmp(full_buf, "vwxyz123", sizeof(full_buf)) == 0);
    sceKernelDeleteMsgPipe(pipe);

    test_exit();
    return 0;
}
