// Whether a priority change that makes a new head of a message pipe's receive queue hands that head
// the bytes the pipe already holds. The pipe has attr 0x2, which queues receivers by priority. The
// receivers run on core 0 and this thread on core 1. Each case uses a new pipe and new threads. The
// main thread first puts "abcd" into the empty pipe with a try send. Receiver A at priority 0x80
// then waits for 8 bytes FULL, and receiver B at 0x88 waits behind it for 4 bytes FULL. The 4
// bytes in the pipe are enough for B. Then the main thread waits up to 1 s for B to end, reads the
// bytes in the pipe and the waiting receivers, and sends "efgh" with a try send. A cancel of the
// pipe ends any receiver still waiting. Three cases:
// - peek, lowered: A receives with DONT_REMOVE, so "abcd" stays in the pipe while A waits. A is
//   then lowered to 0x90, below B, so B becomes the head.
// - peek, kept: the same, but A keeps 0x80, so A stays the head.
// - remove, lowered: A receives without DONT_REMOVE, so it takes "abcd" and the pipe is empty. A
//   is then lowered to 0x90, below B.
// Each case prints the send of "abcd", whether A and B wait, the bytes in the pipe before the
// change, the change, whether B ended within 1 s of it, the bytes in the pipe and the waiting
// receivers then, the send of "efgh", whether A and B wait right after it, the bytes in the pipe
// and the waiting receivers then, the cancel, and each receiver's result, bytes and size.
//
// On a PS Vita TV on firmware 3.65 the priority change doesn't hand the ring's bytes to the
// receiver it makes the head. With A peeking and lowered below B, B still waits a second later and
// the pipe still holds "abcd". The next send ends B with "abcd", and A, behind it, peeks
// "abcdefgh" and keeps waiting until the cancel (0x80028007). Without the change A stays the head,
// so the send ends A with a peek of "abcdefgh" and then B with "abcd". With A removing its bytes
// the pipe is empty before the change, and the send gives B "efgh" while A keeps "abcd".
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define RECEIVERS 2
#define LONG_TIMEOUT (3 * TEST_WAIT_US)
#define END_WAIT_US 1000000
#define NOT_DONE 0xDEAD

#define A_PRIORITY 0x80
#define B_PRIORITY 0x88
#define LOWERED 0x90

static const char *const names[RECEIVERS] = { "A", "B" };

static SceUID pipe;
static SceUID threads[RECEIVERS];
static SceUInt32 modes[RECEIVERS];
static SceSize sizes[RECEIVERS];
static volatile int result[RECEIVERS];
static SceSize moved[RECEIVERS];
static char received[RECEIVERS][16];

static int receiver(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = LONG_TIMEOUT;
    result[i] = sceKernelReceiveMsgPipe(pipe, received[i], sizes[i], modes[i], &moved[i], &timeout);
    return 0;
}

// Starts receiver i on core 0 and waits until it is in its wait or has ended
static void start(int i, int priority, SceUInt32 mode, SceSize size) {
    result[i] = NOT_DONE;
    moved[i] = NOT_DONE;
    modes[i] = mode;
    sizes[i] = size;
    sceClibMemset(received[i], 0, sizeof(received[i]));
    threads[i] = sceKernelCreateThread("receiver", receiver, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(threads[i], sizeof(i), &i);
    TEST_WAIT_FOR(result[i] != NOT_DONE || test_in_wait(threads[i], WAIT_TYPE_MSG_PIPE, pipe));
}

static SceKernelMppInfo pipe_info(void) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    return info;
}

static int pipe_used(void) {
    return CAPACITY - pipe_info().freeSize;
}

// Runs one case. A receives with a_mode, and its priority changes to a_new_priority after B waits.
static void run(const char *name, SceUInt32 a_mode, int a_new_priority) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, MSG_PIPE_ATTR_RECEIVE_PRIO, CAPACITY, NULL);

    SceSize first_size = NOT_DONE;
    const int first_send = sceKernelTrySendMsgPipe(pipe, "abcd", 4, MSG_PIPE_MODE_FULL, &first_size);

    start(0, A_PRIORITY, a_mode, 8);
    const int a_waits = test_in_wait(threads[0], WAIT_TYPE_MSG_PIPE, pipe);
    start(1, B_PRIORITY, MSG_PIPE_MODE_FULL, 4);
    const int b_waits = test_in_wait(threads[1], WAIT_TYPE_MSG_PIPE, pipe);
    const int used_before = pipe_used();

    int change = NOT_DONE;
    if (a_new_priority != A_PRIORITY)
        change = sceKernelChangeThreadPriority(threads[0], a_new_priority);
    SceUInt end_timeout = END_WAIT_US;
    const int b_ended = sceKernelWaitThreadEnd(threads[1], NULL, &end_timeout) == 0;
    const int used_after_wait = pipe_used();
    const int waiting_after_wait = pipe_info().numReceiveWaitThreads;

    SceSize last_size = NOT_DONE;
    const int last_send = sceKernelTrySendMsgPipe(pipe, "efgh", 4, MSG_PIPE_MODE_FULL, &last_size);
    // Read right after the send, so they show what the send itself did
    int waits_after_send[RECEIVERS];
    for (int i = 0; i < RECEIVERS; i++)
        waits_after_send[i] = test_in_wait(threads[i], WAIT_TYPE_MSG_PIPE, pipe);
    const int used_after_send = pipe_used();
    const int waiting_after_send = pipe_info().numReceiveWaitThreads;

    const int cancel = sceKernelCancelMsgPipe(pipe, NULL, NULL);
    for (int i = 0; i < RECEIVERS; i++)
        test_join_thread(threads[i]);
    sceKernelDeleteMsgPipe(pipe);

    PRINTF(first_send, "%s: try send \"abcd\"", name);
    PRINTF(first_size, "%s: try send \"abcd\", its size", name);
    PRINTF(a_waits, "%s: A waits", name);
    PRINTF(b_waits, "%s: B waits", name);
    PRINTF(used_before, "%s: the bytes in the pipe before the change", name);
    if (a_new_priority != A_PRIORITY)
        PRINTF(change, "%s: lowering A below B", name);
    PRINTF(b_ended, "%s: B ended within 1 s", name);
    PRINTF(used_after_wait, "%s: the bytes in the pipe after that", name);
    PRINTF(waiting_after_wait, "%s: the waiting receivers after that", name);
    PRINTF(last_send, "%s: try send \"efgh\"", name);
    PRINTF(last_size, "%s: try send \"efgh\", its size", name);
    for (int i = 0; i < RECEIVERS; i++)
        PRINTF(waits_after_send[i], "%s: %s waits after it", name, names[i]);
    PRINTF(used_after_send, "%s: the bytes in the pipe after it", name);
    PRINTF(waiting_after_send, "%s: the waiting receivers after it", name);
    PRINTF(cancel, "%s: the cancel", name);
    for (int i = 0; i < RECEIVERS; i++) {
        PRINTF(result[i], "%s: %s's result", name, names[i]);
        PRINTF(moved[i], "%s: %s got \"%s\", its size", name, names[i], received[i]);
    }
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);

    run("peek, lowered", MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_REMOVE, LOWERED);
    run("peek, kept", MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_REMOVE, A_PRIORITY);
    run("remove, lowered", MSG_PIPE_MODE_FULL, LOWERED);

    test_exit();
    return 0;
}
