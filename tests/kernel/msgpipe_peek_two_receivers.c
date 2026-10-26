// What three receivers get from one blocking FULL send on a message pipe, and what the pipe holds
// after the receivers ended. Three threads wait on an empty pipe in this order: receiver 1 and
// receiver 2 with DONT_REMOVE FULL 4, then receiver 3 with a plain FULL 4. The main thread then
// sends "pqrs" with a blocking FULL send. The test prints each receiver's result, size and bytes,
// the send's result and size, and the waiting receivers before the send and after the receivers
// ended. Once every receiver has ended, a FULL try receive of 4 runs with nothing waiting, and the
// test prints its result, size, bytes and the pipe's free size after it. msgpipe_peek_stale covers
// the two DONT_REMOVE receivers without the third one. Every receiver is joined before its values
// are read, and nothing waits on the pipe while the try receive runs.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define NOT_WRITTEN 0xDEAD
#define RECEIVERS 3

typedef struct Receiver {
    SceUInt32 mode;
    int size;
} Receiver;

static const Receiver receivers[RECEIVERS] = {
    { MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_REMOVE, 4 },
    { MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_REMOVE, 4 },
    { MSG_PIPE_MODE_FULL, 4 },
};

static SceUID pipe;
static volatile int result[RECEIVERS];
static SceSize moved[RECEIVERS];
static char received[RECEIVERS][16];

static int receiver_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    result[i] = sceKernelReceiveMsgPipe(pipe, received[i], receivers[i].size, receivers[i].mode, &moved[i], &timeout);
    return 0;
}

static SceKernelMppInfo pipe_info(void) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    return info;
}

int main(void) {
    SceUID threads[RECEIVERS];
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    for (int i = 0; i < RECEIVERS; i++) {
        result[i] = NOT_WRITTEN;
        moved[i] = NOT_WRITTEN;
        sceClibMemset(received[i], 0, sizeof(received[i]));
        threads[i] = test_start_thread_arg(receiver_thread, i);
        test_wait_until_waiting(threads[i]);
        PRINTF(test_in_wait(threads[i], WAIT_TYPE_MSG_PIPE, pipe), "receiver %d waits on the pipe", i + 1);
    }
    PRINT("the waiting receivers before the send", pipe_info().numReceiveWaitThreads);

    SceSize size = NOT_WRITTEN;
    SceUInt32 timeout = TEST_WAIT_US;
    PRINT("send \"pqrs\"", sceKernelSendMsgPipe(pipe, "pqrs", 4, MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINT("send \"pqrs\", its size", size);
    for (int i = 0; i < RECEIVERS; i++)
        test_join_thread(threads[i]);
    for (int i = 0; i < RECEIVERS; i++) {
        PRINTF(result[i], "receiver %d's result", i + 1);
        PRINTF(moved[i], "receiver %d got \"%s\", its size", i + 1, received[i]);
    }
    PRINT("the waiting receivers after the receivers ended", pipe_info().numReceiveWaitThreads);
    PRINT("the free size after the receivers ended", pipe_info().freeSize);

    char buf[16];
    sceClibMemset(buf, 0, sizeof(buf));
    size = NOT_WRITTEN;
    PRINT("try receive of 4 after them", sceKernelTryReceiveMsgPipe(pipe, buf, 4, MSG_PIPE_MODE_FULL, &size));
    PRINTF(size, "try receive of 4 after them, it got \"%s\", its size", buf);
    PRINT("the free size after the try receive", pipe_info().freeSize);

    sceKernelDeleteMsgPipe(pipe);
    test_exit();
    return 0;
}
