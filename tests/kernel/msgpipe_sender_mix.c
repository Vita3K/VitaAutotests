// Message pipe receives that hand bytes to several waiting senders in one call. The pipe is filled
// with '.' bytes, and three senders queue one at a time, each in its own thread: S1 ASAP
// "ABCDEFGH", S2 FULL "ij" and S3 ASAP "KLMNOP". Two blocking ASAP receives follow, of the sizes
// the case gives, and then a drain that takes what the pipe holds. The test checks which senders
// are served and how far: a FULL sender needs all of its rest to fit, an ASAP sender takes what
// fits and is done with that size, and the walk stops at the first sender that gets no room.
// Senders queue in arrival order for attr 0x0 and 0x2, and by priority for attr 0x1 and 0x3. The
// senders have the same priorities in every case, S2 highest, S3 in the middle and S1 lowest, so
// the cases differ only in attr. The cases:
// - attr 0x0, receives of 5 and 3
// - attr 0x1, receives of 5 and 4
// - attr 0x2, receives of 5 and 4
// - attr 0x3, receives of 5 and 4
// After each receive the test prints the receive's result, its size and the bytes other than '.'
// it got, then the free size, the waiting senders and each sender's done flag, result and size.
// The drain prints the bytes other than '.' it got. Then the pipe is canceled, which ends the
// senders left, and deleted. Only blocking calls with timeouts run here, no try receive, since a
// try receive that leaves a sender waiting can fail to return.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// A Vita rounds a pipe's buffer up to 4 KiB, so the pipe is created with that size
#define CAPACITY 4096
#define SENDERS 3
#define MAIN_PRIORITY 0x60
#define HIGHEST 0x80
#define MIDDLE 0x88
#define LOWEST 0x90

// One sender: its priority, its send mode, its size and its bytes
typedef struct Sender {
    int priority;
    SceUInt32 mode;
    SceSize size;
    const char *data;
} Sender;

// A case: the pipe's attr, the senders in the order they are started, and the sizes of up to two
// receives, a size of 0 for none
typedef struct Case {
    const char *name;
    SceUInt32 attr;
    Sender senders[SENDERS];
    SceSize receives[2];
} Case;

static const Case *current;
static SceUID pipe;
static SceUID threads[SENDERS];
static volatile int result[SENDERS], done[SENDERS];
static SceSize moved[SENDERS];
static char fill[CAPACITY], drained[CAPACITY];
// The bytes other than '.' that the last receive or drain got, in order
static char seen[64];
static int seen_len;

static int sender(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    const Sender *s = &current->senders[i];
    SceUInt32 timeout = 2 * TEST_WAIT_US;
    result[i] = sceKernelSendMsgPipe(pipe, (void *)s->data, s->size, s->mode, &moved[i], &timeout);
    done[i] = 1;
    return 0;
}

// Waits until each sender is done or waits on the pipe, on 20 polls in a row, at most 5 s. A sender
// that was just woken may show its wait until it runs, so one look isn't enough.
static void settle(void) {
    int stable = 0;
    for (int poll = 0; poll < 5000 && stable < 20; poll++) {
        int all = 1;
        for (int i = 0; i < SENDERS; i++)
            all = all && (done[i] || test_in_wait(threads[i], WAIT_TYPE_MSG_PIPE, pipe));
        stable = all ? stable + 1 : 0;
        if (stable < 20)
            sceKernelDelayThread(1000);
    }
}

// Prints the free size and the waiting senders, then each sender's done flag, result and size
static void print_state(const char *name, const char *step) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    PRINTF(info.freeSize, "%s, %s: the free size", name, step);
    PRINTF(info.numSendWaitThreads, "%s, %s: the waiting senders", name, step);
    for (int i = 0; i < SENDERS; i++) {
        PRINTF(done[i], "%s, %s: sender %d is done", name, step, i + 1);
        PRINTF(result[i], "%s, %s: sender %d's result", name, step, i + 1);
        PRINTF(moved[i], "%s, %s: sender %d's size", name, step, i + 1);
    }
}

// Adds the bytes of buf other than '.' to seen
static void collect(const char *buf, SceSize size) {
    for (SceSize i = 0; i < size && i < CAPACITY; i++) {
        if (buf[i] != '.' && seen_len < (int)sizeof(seen) - 1)
            seen[seen_len++] = buf[i];
    }
    seen[seen_len] = '\0';
}

// A blocking ASAP receive of size bytes, which the pipe's ring holds, and what it got
static void receive_step(const char *name, int step, SceSize size) {
    SceSize got = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;
    const int r = sceKernelReceiveMsgPipe(pipe, drained, size, MSG_PIPE_MODE_ASAP, &got, &timeout);
    seen_len = 0;
    seen[0] = '\0';
    collect(drained, got);
    PRINTF(r, "%s, receive %d: the result", name, step);
    PRINTF(got, "%s, receive %d: the size", name, step);
    PRINTF(seen_len, "%s, receive %d: the bytes other than '.' are \"%s\"", name, step, seen);
}

// A blocking ASAP receive of the whole pipe, which takes what the pipe holds, and what it got
static void drain(const char *name) {
    SceSize got = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;
    const int r =
        sceKernelReceiveMsgPipe(pipe, drained, CAPACITY, MSG_PIPE_MODE_ASAP, &got, &timeout);
    seen_len = 0;
    seen[0] = '\0';
    collect(drained, got);
    PRINTF(r, "%s, drain: the result", name);
    PRINTF(got, "%s, drain: the size", name);
    PRINTF(seen_len, "%s, drain: the bytes other than '.' are \"%s\"", name, seen);
}

// Cancels the pipe, which ends the senders still waiting, joins the senders and prints the ends
static void end_case(const char *name) {
    int ended = -1, receivers = -1;
    const int canceled = sceKernelCancelMsgPipe(pipe, &ended, &receivers);
    for (int i = 0; i < SENDERS; i++)
        test_join_thread(threads[i]);
    PRINTF(canceled, "%s, cancel: the result", name);
    PRINTF(ended, "%s, cancel: the senders it ended", name);
    for (int i = 0; i < SENDERS; i++) {
        PRINTF(result[i], "%s, end: sender %d's result", name, i + 1);
        PRINTF(moved[i], "%s, end: sender %d's size", name, i + 1);
    }
    sceKernelDeleteMsgPipe(pipe);
}

static void run_case(const Case *c) {
    current = c;
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, c->attr, CAPACITY, NULL);
    sceKernelTrySendMsgPipe(pipe, fill, CAPACITY, MSG_PIPE_MODE_FULL, NULL);
    for (int i = 0; i < SENDERS; i++) {
        result[i] = 0xDEAD;
        done[i] = 0;
        moved[i] = 0xDEAD;
        threads[i] = sceKernelCreateThread("sender", sender, c->senders[i].priority, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(threads[i], sizeof(i), &i);
        test_wait_until_waiting(threads[i]);
    }

    for (int r = 0; r < 2 && c->receives[r] > 0; r++) {
        receive_step(c->name, r + 1, c->receives[r]);
        settle();
        print_state(c->name, r == 0 ? "after receive 1" : "after receive 2");
    }
    drain(c->name);
    end_case(c->name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);
    sceClibMemset(fill, '.', sizeof(fill));

    static const Case cases[] = {
        { "attr 0x0", 0x0,
          { { LOWEST, MSG_PIPE_MODE_ASAP, 8, "ABCDEFGH" },
            { HIGHEST, MSG_PIPE_MODE_FULL, 2, "ij" },
            { MIDDLE, MSG_PIPE_MODE_ASAP, 6, "KLMNOP" } },
          { 5, 3 } },
        { "attr 0x1", 0x1,
          { { LOWEST, MSG_PIPE_MODE_ASAP, 8, "ABCDEFGH" },
            { HIGHEST, MSG_PIPE_MODE_FULL, 2, "ij" },
            { MIDDLE, MSG_PIPE_MODE_ASAP, 6, "KLMNOP" } },
          { 5, 4 } },
        { "attr 0x2", 0x2,
          { { LOWEST, MSG_PIPE_MODE_ASAP, 8, "ABCDEFGH" },
            { HIGHEST, MSG_PIPE_MODE_FULL, 2, "ij" },
            { MIDDLE, MSG_PIPE_MODE_ASAP, 6, "KLMNOP" } },
          { 5, 4 } },
        { "attr 0x3", 0x3,
          { { LOWEST, MSG_PIPE_MODE_ASAP, 8, "ABCDEFGH" },
            { HIGHEST, MSG_PIPE_MODE_FULL, 2, "ij" },
            { MIDDLE, MSG_PIPE_MODE_ASAP, 6, "KLMNOP" } },
          { 5, 4 } },
    };
    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        run_case(&cases[i]);

    test_exit();
    return 0;
}
