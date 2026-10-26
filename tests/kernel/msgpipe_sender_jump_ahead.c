// Whether a message pipe send that comes in behind a waiting head sender goes on past it. The head
// H is a FULL send of 10 bytes at priority 0xB0, and it waits on a pipe with 8 free bytes. The test
// checks whether a new sender of higher priority than the head goes on at once when it fits, and
// otherwise queues in front of the head, and whether a new sender of equal or lower priority, or of
// any priority with attr 0x0, queues behind the head. It also checks whether a try send and a send
// with a timeout of 0 follow the same rule. The main thread runs at 0x60 and every thread is pinned
// to core 0, so only one thread runs at a time, and a thread started at a lower priority than the
// main thread runs when the main thread blocks. The cases, each on a fresh pipe:
// - attr 0x1, a FULL send of "xy" from a thread at 0x70
// - attr 0x1, the same send from a thread at 0xB0
// - attr 0x0, the same send from a thread at 0x70
// - attr 0x1, a FULL send of 12 from a thread at 0x70, then two blocking ASAP receives of 2
// - attr 0x0, the same send and receives
// - attr 0x1 and attr 0x0, the main thread's try send of "xy"
// - attr 0x1 and attr 0x0, a FULL send of "xy" with a timeout of 0 from a thread at 0x70
// After each step the test prints the free size, the waiting senders and, for each sender, whether
// it is done, whether it waits on the pipe, and its result and size. The pipe is canceled at the
// end of each case, which ends the sends still waiting, and the threads are joined. An ASAP try send
// from another thread while the head waits is left out, since it froze a PS Vita TV.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// A Vita rounds a pipe's buffer up to 4 KiB, so the pipe is created with that size
#define CAPACITY 4096
// The room the pipe has left when the head queues
#define FREE 8
#define MAIN_PRIORITY 0x60
#define NEW_PRIORITY 0x70
#define HEAD_PRIORITY 0xB0
// The head is sender 0 and the sender a case adds is sender 1
#define HEAD 0
#define NEW_SENDER 1
#define SENDERS 2

// What a sender thread does: a try send, or a blocking send with this timeout
typedef struct Job {
    int try_send;
    SceUInt32 mode;
    SceSize size;
    const char *data;
    SceUInt32 timeout;
} Job;

static SceUID pipe;
static SceUID threads[SENDERS];
static Job jobs[SENDERS];
static volatile int result[SENDERS], done[SENDERS];
static SceSize moved[SENDERS];
static char fill[CAPACITY], received[16];

static int sender(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    const Job *job = &jobs[i];
    SceUInt32 timeout = job->timeout;
    if (job->try_send)
        result[i] = sceKernelTrySendMsgPipe(pipe, (void *)job->data, job->size, job->mode,
                                            &moved[i]);
    else
        result[i] = sceKernelSendMsgPipe(pipe, (void *)job->data, job->size, job->mode, &moved[i],
                                         &timeout);
    done[i] = 1;
    return 0;
}

// Starts sender i with its job at priority, on core 0
static void start_sender(int i, int priority) {
    result[i] = 0xDEAD;
    done[i] = 0;
    moved[i] = 0xDEAD;
    threads[i] = sceKernelCreateThread("sender", sender, priority, 0x4000, 0,
                                       SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(threads[i], sizeof(i), &i);
}

// Creates a pipe with attr that holds FREE bytes of room, then starts the head and waits until it
// waits on the pipe
static void start_head(SceUInt32 attr) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, attr, CAPACITY, NULL);
    sceKernelTrySendMsgPipe(pipe, fill, CAPACITY - FREE, MSG_PIPE_MODE_FULL, NULL);
    jobs[HEAD] = (Job){ 0, MSG_PIPE_MODE_FULL, 10, "0123456789", 2 * TEST_WAIT_US };
    start_sender(HEAD, HEAD_PRIORITY);
    test_wait_until_waiting(threads[HEAD]);
}

// Waits until each of the first started senders is done or waits on the pipe, on 20 polls in a
// row, at most 5 s. A sender that was just woken may show its wait until it runs, so one look isn't
// enough.
static void settle(int started) {
    int stable = 0;
    for (int poll = 0; poll < 5000 && stable < 20; poll++) {
        int all = 1;
        for (int i = 0; i < started; i++)
            all = all && (done[i] || test_in_wait(threads[i], WAIT_TYPE_MSG_PIPE, pipe));
        stable = all ? stable + 1 : 0;
        if (stable < 20)
            sceKernelDelayThread(1000);
    }
}

// Prints the free size and the waiting senders, then for each of the first started senders whether
// it is done, whether it waits on the pipe, and its result and size
static void print_state(const char *name, const char *step, int started) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    PRINTF(info.freeSize, "%s, %s: the free size", name, step);
    PRINTF(info.numSendWaitThreads, "%s, %s: the waiting senders", name, step);
    for (int i = 0; i < started; i++) {
        const char *who = i == HEAD ? "the head" : "the new sender";
        PRINTF(done[i], "%s, %s: %s is done", name, step, who);
        PRINTF(test_in_wait(threads[i], WAIT_TYPE_MSG_PIPE, pipe), "%s, %s: %s waits", name, step,
               who);
        PRINTF(result[i], "%s, %s: %s's result", name, step, who);
        PRINTF(moved[i], "%s, %s: %s's size", name, step, who);
    }
}

// A blocking ASAP receive of size bytes, which the ring holds, and its result and size
static void receive_step(const char *name, const char *step, SceSize size) {
    SceSize got = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;
    const int r = sceKernelReceiveMsgPipe(pipe, received, size, MSG_PIPE_MODE_ASAP, &got, &timeout);
    PRINTF(r, "%s, %s: the receive's result", name, step);
    PRINTF(got, "%s, %s: the receive's size", name, step);
}

// Cancels the pipe, which ends the senders still waiting, then joins the started senders and prints
// how they ended
static void end_case(const char *name, int started) {
    int ended = -1, receivers = -1;
    const int canceled = sceKernelCancelMsgPipe(pipe, &ended, &receivers);
    for (int i = 0; i < started; i++)
        test_join_thread(threads[i]);
    PRINTF(canceled, "%s, cancel: the result", name);
    PRINTF(ended, "%s, cancel: the senders it ended", name);
    for (int i = 0; i < started; i++) {
        PRINTF(result[i], "%s, end: sender %d's result", name, i + 1);
        PRINTF(moved[i], "%s, end: sender %d's size", name, i + 1);
    }
    sceKernelDeleteMsgPipe(pipe);
}

// A FULL send of "xy" from a new thread at priority, behind the head at 0xB0
static void new_send(const char *name, SceUInt32 attr, int priority) {
    start_head(attr);
    jobs[NEW_SENDER] = (Job){ 0, MSG_PIPE_MODE_FULL, 2, "xy", 2 * TEST_WAIT_US };
    start_sender(NEW_SENDER, priority);
    settle(SENDERS);
    print_state(name, "after the send", SENDERS);
    end_case(name, SENDERS);
}

// A FULL send of 12 from a new thread at 0x70, then two receives of 2
static void big_send(const char *name, SceUInt32 attr) {
    start_head(attr);
    jobs[NEW_SENDER] = (Job){ 0, MSG_PIPE_MODE_FULL, 12, "ABCDEFGHIJKL", 2 * TEST_WAIT_US };
    start_sender(NEW_SENDER, NEW_PRIORITY);
    settle(SENDERS);
    print_state(name, "after the send", SENDERS);
    receive_step(name, "receive 1", 2);
    settle(SENDERS);
    print_state(name, "after receive 1", SENDERS);
    receive_step(name, "receive 2", 2);
    settle(SENDERS);
    print_state(name, "after receive 2", SENDERS);
    end_case(name, SENDERS);
}

// The main thread's try send of "xy" on a pipe whose head waits
static void main_try(const char *name, SceUInt32 attr) {
    start_head(attr);
    SceSize size = 0xDEAD;
    const int r = sceKernelTrySendMsgPipe(pipe, "xy", 2, MSG_PIPE_MODE_FULL, &size);
    PRINTF(r, "%s: the try send's result", name);
    PRINTF(size, "%s: the try send's size", name);
    print_state(name, "after the try send", 1);
    end_case(name, 1);
}

// A FULL send of "xy" with a timeout of 0 from a new thread at 0x70
static void zero_timeout(const char *name, SceUInt32 attr) {
    start_head(attr);
    jobs[NEW_SENDER] = (Job){ 0, MSG_PIPE_MODE_FULL, 2, "xy", 0 };
    start_sender(NEW_SENDER, NEW_PRIORITY);
    settle(SENDERS);
    print_state(name, "after the send", SENDERS);
    end_case(name, SENDERS);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);
    sceClibMemset(fill, '.', sizeof(fill));

    new_send("attr 0x1, FULL 2 from 0x70", 0x1, NEW_PRIORITY);
    new_send("attr 0x1, FULL 2 from 0xB0", 0x1, HEAD_PRIORITY);
    new_send("attr 0x0, FULL 2 from 0x70", 0x0, NEW_PRIORITY);
    big_send("attr 0x1, FULL 12 from 0x70", 0x1);
    big_send("attr 0x0, FULL 12 from 0x70", 0x0);
    main_try("attr 0x1, main try FULL 2", 0x1);
    main_try("attr 0x0, main try FULL 2", 0x0);
    zero_timeout("attr 0x1, FULL 2 timeout 0 from 0x70", 0x1);
    zero_timeout("attr 0x0, FULL 2 timeout 0 from 0x70", 0x0);

    test_exit();
    return 0;
}
