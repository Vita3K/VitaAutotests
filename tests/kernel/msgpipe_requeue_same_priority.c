// What a priority change that sets the head of a message pipe's queue to the priority it already
// has does, when the head already moved part of its bytes and when it moved none. Two parts.
//
// Receivers: receiver A waits for 8 bytes FULL and queues first, then B, of lower priority, waits
// for 4 bytes ASAP behind it, and a send of "abcd" gives A its first 4 bytes. Then A is set to the
// priority it has, and the test sends 4 bytes at a time with a try send, "efgh", "ijkl" and, with
// three receivers, "mnop", each once the one before has woken a receiver. Three cases:
// - attr 0x2, which queues receivers by priority
// - attr 0x2, and after the change receiver C, of higher priority than A and B, waits for 4 bytes
//   ASAP
// - attr 0x0, FIFO
// Each case prints how many receivers were done within 500 ms of the first send and of the
// change, which receiver each later send woke, as 0xA, 0xB or 0xC, and what each one got. A
// receive that doesn't end by then ends when the pipe is deleted.
//
// Senders, the reference for a head that moved nothing: the pipe is filled with '.' up to 4 free
// bytes. Sender A sends "abcdefgh" FULL, which doesn't fit, and queues first, then B, of lower
// priority, sends "ABCD" FULL, which would fit, and queues behind it. Then A is set to the priority
// it has. Two cases, attr 0x1, which queues senders by priority, and attr 0x0, FIFO. Each case
// prints the senders done within 500 ms of the change, the free size and the waiting senders
// after it. Then the pipe is canceled, which ends the sends left.
//
// On a PS Vita TV on firmware 3.65 neither "abcd" nor the change ends a receive. With attr 0x2 the
// change still moves A, which holds "abcd", behind B: B takes "efgh" and A ends with "abcdijkl".
// The flag that kept A first stays set after the change, so C, queued next, lands behind B instead
// of in front of it: "efgh" goes to B, "ijkl" to C, and A ends with "abcdmnop". The FIFO pipe
// gives A "abcdefgh" and B "ijkl". For the senders the change does nothing with either attr: A,
// which moved nothing, keeps the front, nobody is done, 4 bytes stay free, and the cancel ends both
// sends with WAIT_CANCEL (0x80028007) and a size of 0.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define FREE 4
#define HIGH 0x78
#define A_PRIORITY 0x80
#define B_PRIORITY 0x88
#define WAITERS 3

static const char *const names[WAITERS] = { "A", "B", "C" };
static const char *const sends[WAITERS] = { "abcdefgh", "ABCD", "" };

static SceUID pipe;
static SceUID threads[WAITERS];
static volatile int sizes[WAITERS];
static volatile SceUInt32 modes[WAITERS];
static volatile int result[WAITERS], done_count, woke[WAITERS];
static SceSize moved[WAITERS];
static char received[WAITERS][16];
static char fill[CAPACITY];

static int receiver(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    result[i] = sceKernelReceiveMsgPipe(pipe, received[i], sizes[i], modes[i], &moved[i], &timeout);
    const int n = __atomic_fetch_add(&done_count, 1, __ATOMIC_SEQ_CST);
    if (n < WAITERS)
        woke[n] = 0xA + i;
    return 0;
}

static int sender(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    result[i] = sceKernelSendMsgPipe(pipe, (void *)sends[i], sceClibStrnlen(sends[i], 16), MSG_PIPE_MODE_FULL,
                                     &moved[i], &timeout);
    const int n = __atomic_fetch_add(&done_count, 1, __ATOMIC_SEQ_CST);
    if (n < WAITERS)
        woke[n] = 0xA + i;
    return 0;
}

// Starts waiter i and waits until it waits
static void start(SceKernelThreadEntry entry, int i, int size, SceUInt32 mode, int priority) {
    result[i] = 0xDEAD;
    woke[i] = 0;
    moved[i] = 0;
    sceClibMemset(received[i], 0, sizeof(received[i]));
    sizes[i] = size;
    modes[i] = mode;
    threads[i] = sceKernelCreateThread("waiter", entry, priority, 0x4000, 0, 0, NULL);
    sceKernelStartThread(threads[i], sizeof(i), &i);
    test_wait_until_waiting(threads[i]);
}

// Waits up to 500 ms for a waiter to be done, for an outcome that would come at once
static int done_soon(void) {
    for (int t = 0; t < 500 && done_count == 0; t++)
        sceKernelDelayThread(1000);
    return done_count;
}

static void send(const char *bytes) {
    SceSize size;
    sceKernelTrySendMsgPipe(pipe, (void *)bytes, 4, MSG_PIPE_MODE_FULL, &size);
}

static void receivers_case(const char *name, SceUInt32 attr, int with_c) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, attr, CAPACITY, NULL);
    done_count = 0;
    woke[2] = 0;
    start(receiver, 0, 8, MSG_PIPE_MODE_FULL, A_PRIORITY);
    start(receiver, 1, 4, MSG_PIPE_MODE_ASAP, B_PRIORITY);

    send("abcd");
    const int after_first = done_soon();
    const int changed = sceKernelChangeThreadPriority(threads[0], A_PRIORITY);
    const int after_change = done_soon();
    const int count = with_c ? 3 : 2;
    if (with_c)
        start(receiver, 2, 4, MSG_PIPE_MODE_ASAP, HIGH);
    static const char *const later[3] = { "efgh", "ijkl", "mnop" };
    for (int n = 0; n < count; n++) {
        send(later[n]);
        TEST_WAIT_FOR(done_count >= n + 1);
    }
    sceKernelDeleteMsgPipe(pipe);
    // Joined before the records are read, since a receiver counts itself before it writes its own
    for (int i = 0; i < count; i++)
        test_join_thread(threads[i]);

    PRINTF(after_first, "%s: receivers done after \"abcd\"", name);
    PRINTF(changed, "%s: the change", name);
    PRINTF(after_change, "%s: receivers done after the change", name);
    for (int n = 0; n < count; n++)
        PRINTF(woke[n], "%s: the receiver \"%s\" woke", name, later[n]);
    for (int i = 0; i < count; i++) {
        PRINTF(result[i], "%s: %s's result", name, names[i]);
        PRINTF(moved[i], "%s: %s got \"%s\", size", name, names[i], received[i]);
    }
}

static void senders_case(const char *name, SceUInt32 attr) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, attr, CAPACITY, NULL);
    done_count = 0;
    sceKernelTrySendMsgPipe(pipe, fill, CAPACITY - FREE, MSG_PIPE_MODE_FULL, NULL);
    start(sender, 0, 0, 0, A_PRIORITY);
    start(sender, 1, 0, 0, B_PRIORITY);

    const int changed = sceKernelChangeThreadPriority(threads[0], A_PRIORITY);
    const int after_change = done_soon();
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    int waiting = -1, receivers = -1;
    const int canceled = sceKernelCancelMsgPipe(pipe, &waiting, &receivers);
    for (int i = 0; i < 2; i++)
        test_join_thread(threads[i]);
    sceKernelDeleteMsgPipe(pipe);

    PRINTF(changed, "%s: the change", name);
    PRINTF(after_change, "%s: senders done after the change", name);
    PRINTF(info.freeSize, "%s: the free size after the change", name);
    PRINTF(info.numSendWaitThreads, "%s: the waiting senders after the change", name);
    PRINTF(canceled, "%s: cancel the pipe", name);
    PRINTF(waiting, "%s: the senders the cancel ended", name);
    for (int i = 0; i < 2; i++) {
        PRINTF(result[i], "%s: %s's result", name, names[i]);
        PRINTF(moved[i], "%s: %s's size sent", name, names[i]);
    }
}

int main(void) {
    sceClibMemset(fill, '.', sizeof(fill));

    receivers_case("receivers attr 0x2", MSG_PIPE_ATTR_RECEIVE_PRIO, 0);
    receivers_case("receivers attr 0x2, then C", MSG_PIPE_ATTR_RECEIVE_PRIO, 1);
    receivers_case("receivers attr 0x0", 0x0, 0);
    senders_case("senders attr 0x1", 0x1);
    senders_case("senders attr 0x0", 0x0);

    test_exit();
    return 0;
}
