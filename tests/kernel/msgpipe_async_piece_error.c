// Asynchronous message pipe sends of pieces the pipe can't take, and the error event they leave on
// the pipe. Each case uses a fresh 4 KiB pipe. The first case sends one 0x2000 byte piece with a
// DONT_WAIT FULL send. The second sends a vector of a 4 byte piece and an empty piece with a NULL
// buffer, with the same mode. The test prints what each call returns and the size it wrote back,
// then waits for the pipe's error event bit 0x10 the way msgpipe_async waits for its events, and
// prints the bits and both halves of the user data. A wait that gets no such event times out, and
// its output shows the values it was preset to. The DONT_WAIT sends and receives that succeed are
// in msgpipe_async, so this test only covers the ones that fail.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// The event bit a message pipe sets when one of its calls fails
#define EVENT_ERROR 0x10

static SceUID pipe;
static char piece[0x2000];
static char four[4] = { 1, 2, 3, 4 };

// Prints whether the pipe reports the error event, with the user data split into its two halves
static void print_error_event(const char *name) {
    SceUInt32 pattern = 0xDEAD;
    SceUInt64 user_data = 0xDEADDEADDEADDEADull;
    SceUInt32 timeout = 100000;
    PRINTF(sceKernelWaitEvent(pipe, EVENT_ERROR, &pattern, &user_data, &timeout), "%s: wait for the error event", name);
    PRINTF(pattern, "%s: the event's bits", name);
    PRINTF((int)(user_data & 0xFFFFFFFF), "%s: the low half of its user data", name);
    PRINTF((int)(user_data >> 32), "%s: the high half of its user data", name);
}

int main(void) {
    SceSize size;

    // One piece bigger than the pipe
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 0x1000, NULL);
    size = 0xDEAD;
    PRINT("DONT_WAIT FULL send of 0x2000 bytes to a 4 KiB pipe", sceKernelSendMsgPipe(pipe, piece, sizeof(piece), MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_WAIT, &size, NULL));
    PRINT("its size", size);
    print_error_event("the oversized send");
    sceKernelDeleteMsgPipe(pipe);

    // A 4 byte piece, then an empty piece with no buffer
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 0x1000, NULL);
    const MsgPipeVector pieces[] = { { four, sizeof(four) }, { NULL, 0 } };
    size = 0xDEAD;
    PRINT("DONT_WAIT FULL vector send of 4 bytes and an empty piece", sceKernelSendMsgPipeVector(pipe, pieces, 2, MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_WAIT, &size, NULL));
    PRINT("its size", size);
    print_error_event("the vector send");
    sceKernelDeleteMsgPipe(pipe);

    test_exit();
    return 0;
}
