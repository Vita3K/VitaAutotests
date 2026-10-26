// A blocking message pipe receive that takes the bytes in the ring first, then the bytes of the
// waiting senders in queue order, and then puts the rest of each sender into the ring when it fits.
// Each case fills a 4 KiB pipe with '.' bytes, leaving some room, and queues its senders one at a
// time, each in its own thread with a long timeout. This thread then receives, with a timeout, and
// prints the result and the size of each receive, the bytes other than '.' that it got, the free
// size, the waiting senders and which senders are done after it. A case with drain set then takes
// what is left with a blocking ASAP receive. Last, the pipe is canceled, and each sender prints its
// result and the size it sent. The cases:
// - 6 bytes of room, FULL senders of 8 and 8, a FULL receive of 4096
// - the same with an ASAP receive of 4096
// - attr 0x1, which queues senders by priority, with the high priority sender queued second
// - 96 bytes of room, FULL senders of 100 and 4096, FULL receives of 4050 and then 50
// - 6 bytes of room, a FULL sender of 8, a FULL receive of 4097, which is more than the pipe holds
// Only blocking calls run here, since a try receive can fail to return while a sender waits. The
// senders left are ended by the cancel, and the pipe is deleted after it. The receive of 4097 goes
// last, so an implementation that fails on it loses nothing else.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// A Vita rounds a pipe's buffer up to 4 KiB, so the pipe is created with that size
#define CAPACITY 4096
#define MAX_SENDERS 2
#define MAX_RECEIVES 2
// One priority below the test's thread and one above it, for the senders of attr 0x1
#define LOW_PRIORITY (SCE_KERNEL_DEFAULT_PRIORITY_USER + 2)
#define HIGH_PRIORITY (SCE_KERNEL_DEFAULT_PRIORITY_USER - 2)
// How many of the bytes other than '.' a line shows
#define SEEN_MAX 32

typedef struct Sender {
    SceInt32 priority;
    SceUInt32 mode;
    int size;
    const char *data;
} Sender;

typedef struct Receive {
    SceUInt32 mode;
    int size;
} Receive;

typedef struct Case {
    const char *name;
    SceUInt32 attr;
    // The bytes of room the pipe has when the senders queue
    int room;
    int sender_count;
    Sender senders[MAX_SENDERS];
    int receive_count;
    Receive receives[MAX_RECEIVES];
    // Whether a blocking ASAP receive takes what is left in the pipe at the end
    int drain;
} Case;

static const Case *current;
static SceUID pipe;
static SceUID threads[MAX_SENDERS];
static volatile int result[MAX_SENDERS], done[MAX_SENDERS];
static SceSize moved[MAX_SENDERS];
static char fill[CAPACITY], letters_a[CAPACITY], letters_b[CAPACITY];
// Room past the pipe's size, so a receive of 4097 has a buffer that holds it
static char buffer[CAPACITY + 16];
// How many bytes other than '.' the last receive or drain got, and the first of them
static int others;
static char seen[SEEN_MAX + 1];

static int sender_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    const Sender *sender = &current->senders[i];
    SceUInt32 timeout = 2 * TEST_WAIT_US;
    result[i] = sceKernelSendMsgPipe(pipe, (void *)sender->data, sender->size, sender->mode, &moved[i], &timeout);
    done[i] = 1;
    return 0;
}

// Starts sender i of the current case at its priority, and waits until it waits on the pipe
static void start_sender(int i) {
    threads[i] = sceKernelCreateThread("sender", sender_thread, current->senders[i].priority, 0x4000, 0, 0, NULL);
    sceKernelStartThread(threads[i], sizeof(i), &i);
    test_wait_until_waiting(threads[i]);
}

static int still_waits(int i) {
    return !done[i] && test_in_wait(threads[i], WAIT_TYPE_MSG_PIPE, pipe);
}

// Waits until every sender is done or waits on the pipe, on 20 polls in a row, at most 5 s. A
// thread that was just woken may show its wait until it runs, so one look isn't enough.
static void settle(int count) {
    int stable = 0;
    for (int poll = 0; poll < 5000 && stable < 20; poll++) {
        int all = 1;
        for (int i = 0; i < count; i++)
            all = all && (done[i] || still_waits(i));
        stable = all ? stable + 1 : 0;
        if (stable < 20)
            sceKernelDelayThread(1000);
    }
}

static SceKernelMppInfo pipe_info(void) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    return info;
}

// Sets others to the bytes of buf other than '.', and seen to the first of them
static void collect(const char *buf, SceSize size) {
    int kept = 0;
    others = 0;
    for (SceSize i = 0; i < size; i++) {
        if (buf[i] != '.') {
            if (kept < SEEN_MAX)
                seen[kept++] = buf[i];
            others++;
        }
    }
    seen[kept] = '\0';
}

// Runs receive k of the case with a blocking call, and prints what it and the pipe show after it
static void run_receive(const Case *c, int k) {
    const Receive *r = &c->receives[k];
    const int n = k + 1;
    SceSize size = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;
    const int got = sceKernelReceiveMsgPipe(pipe, buffer, r->size, r->mode, &size, &timeout);

    // Every other thread of the test is blocked or done after this, so the values can be read
    settle(c->sender_count);
    // Only a receive that succeeded has bytes to show
    collect(buffer, got == 0 && size <= CAPACITY ? size : 0);
    const SceKernelMppInfo info = pipe_info();

    PRINTF(got, "%s: receive %d, result", c->name, n);
    PRINTF(size, "%s: receive %d, size", c->name, n);
    PRINTF(others, "%s: receive %d gets \"%s\"", c->name, n, seen);
    PRINTF(info.freeSize, "%s: after receive %d, the free size", c->name, n);
    PRINTF(info.numSendWaitThreads, "%s: after receive %d, the waiting senders", c->name, n);
    for (int i = 0; i < c->sender_count; i++) {
        PRINTF(done[i], "%s: after receive %d, sender %d is done", c->name, n, i + 1);
        PRINTF(moved[i], "%s: after receive %d, sender %d's size sent so far", c->name, n, i + 1);
    }
}

// Takes what is left in the pipe with a blocking ASAP receive, and prints it
static void run_drain(const char *name) {
    SceSize size = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;
    const int got = sceKernelReceiveMsgPipe(pipe, buffer, CAPACITY, MSG_PIPE_MODE_ASAP, &size, &timeout);
    collect(buffer, got == 0 && size <= CAPACITY ? size : 0);

    PRINTF(got, "%s: drain, result", name);
    PRINTF(size, "%s: drain, size", name);
    PRINTF(others, "%s: drain gets \"%s\"", name, seen);
    PRINTF(pipe_info().freeSize, "%s: drain, the free size", name);
}

static void run_case(const Case *c) {
    const char *name = c->name;
    current = c;
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, c->attr, CAPACITY, NULL);

    // Fills the pipe with a blocking send, which has room for all of it and returns at once
    SceSize filled = 0;
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelSendMsgPipe(pipe, fill, CAPACITY - c->room, MSG_PIPE_MODE_FULL, &filled, &timeout);

    for (int i = 0; i < c->sender_count; i++) {
        result[i] = 0xDEAD;
        done[i] = 0;
        moved[i] = 0xDEAD;
        start_sender(i);
    }
    const SceKernelMppInfo before = pipe_info();
    PRINTF(before.freeSize, "%s: the free size before the receives", name);
    PRINTF(before.numSendWaitThreads, "%s: the waiting senders before the receives", name);

    for (int k = 0; k < c->receive_count; k++)
        run_receive(c, k);
    if (c->drain)
        run_drain(name);

    int canceled_senders = -1, canceled_receivers = -1;
    const int canceled = sceKernelCancelMsgPipe(pipe, &canceled_senders, &canceled_receivers);
    for (int i = 0; i < c->sender_count; i++)
        test_join_thread(threads[i]);
    PRINTF(canceled, "%s: cancel the pipe", name);
    PRINTF(canceled_senders, "%s: the senders the cancel ended", name);
    for (int i = 0; i < c->sender_count; i++) {
        PRINTF(result[i], "%s: sender %d's result", name, i + 1);
        PRINTF(moved[i], "%s: sender %d's size sent", name, i + 1);
    }
    PRINTF(pipe_info().freeSize, "%s: the free size after the cancel", name);
    sceKernelDeleteMsgPipe(pipe);
}

int main(void) {
    sceClibMemset(fill, '.', sizeof(fill));
    sceClibMemset(letters_a, 'a', sizeof(letters_a));
    sceClibMemset(letters_b, 'b', sizeof(letters_b));

    static const Case cases[] = {
        { "room 6, FULL 8 and FULL 8, receive FULL 4096", 0x0, 6, 2,
          { { SCE_KERNEL_DEFAULT_PRIORITY_USER, MSG_PIPE_MODE_FULL, 8, "ABCDEFGH" },
            { SCE_KERNEL_DEFAULT_PRIORITY_USER, MSG_PIPE_MODE_FULL, 8, "IJKLMNOP" } },
          1, { { MSG_PIPE_MODE_FULL, CAPACITY } }, 1 },
        { "room 6, FULL 8 and FULL 8, receive ASAP 4096", 0x0, 6, 2,
          { { SCE_KERNEL_DEFAULT_PRIORITY_USER, MSG_PIPE_MODE_FULL, 8, "ABCDEFGH" },
            { SCE_KERNEL_DEFAULT_PRIORITY_USER, MSG_PIPE_MODE_FULL, 8, "IJKLMNOP" } },
          1, { { MSG_PIPE_MODE_ASAP, CAPACITY } }, 1 },
        { "attr 0x1, room 6, FULL 8 and FULL 8, receive FULL 4096", 0x1, 6, 2,
          { { LOW_PRIORITY, MSG_PIPE_MODE_FULL, 8, "ABCDEFGH" },
            { HIGH_PRIORITY, MSG_PIPE_MODE_FULL, 8, "IJKLMNOP" } },
          1, { { MSG_PIPE_MODE_FULL, CAPACITY } }, 1 },
        { "room 96, FULL 100 and FULL 4096, receives 4050 and 50", 0x0, 96, 2,
          { { SCE_KERNEL_DEFAULT_PRIORITY_USER, MSG_PIPE_MODE_FULL, 100, letters_a },
            { SCE_KERNEL_DEFAULT_PRIORITY_USER, MSG_PIPE_MODE_FULL, CAPACITY, letters_b } },
          2, { { MSG_PIPE_MODE_FULL, 4050 }, { MSG_PIPE_MODE_FULL, 50 } }, 1 },
        { "room 6, FULL 8, receive FULL 4097", 0x0, 6, 1,
          { { SCE_KERNEL_DEFAULT_PRIORITY_USER, MSG_PIPE_MODE_FULL, 8, "ABCDEFGH" } },
          1, { { MSG_PIPE_MODE_FULL, CAPACITY + 1 } }, 0 },
    };
    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        run_case(&cases[i]);

    test_exit();
    return 0;
}
