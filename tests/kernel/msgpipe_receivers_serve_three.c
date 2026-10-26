// What one message pipe send does for three or more waiting receivers. Each case queues its
// receivers in a fresh pipe of 4096 bytes, one at a time, each in its own thread with a long
// timeout, then this thread sends. The main thread reads the pipe and the receivers right after
// the send, before any call that blocks, and prints only after the woken receivers were joined,
// since printing blocks. Every receive and send has a timeout, and no try call runs. The cases:
// - FIFO: R1 FULL 4, R2 ASAP 4 and R3 FULL 8 wait in that order, and a FULL send of 10 serves them.
//   R3 keeps its 2 bytes, and a FULL send of 6 completes it.
// - attr 0x2: R3 FULL 8 has the highest priority, R2 ASAP 4 the middle and R1 FULL 4 the lowest.
//   The send of 10 gives R3 8 bytes and R2 the 2 left, and R1 waits with nothing until a send of 4.
// - ASAP 2 and ASAP 2 wait, and a FULL send of 6 serves both. The 2 bytes left stay in the pipe,
//   and a FULL receive of 2 takes them.
// - FULL 4 waits, and a FULL send of 4096 gives it 4 bytes. The rest goes into the pipe, and a FULL
//   receive of 4092 takes it in order.
// - FULL 4096 waits, and a FULL send of 4096 gives it all of them, leaving the pipe empty.
// - FULL 8 and ASAP 4 wait, and an ASAP send of 6 gives 6 bytes to FULL 8 and nothing to ASAP 4.
//   A send of 2 completes FULL 8, and a send of 1 completes ASAP 4.
// Every wait has a finite timeout, and the sends and receives are blocking with timeouts.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// A Vita rounds a pipe's buffer up to 4 KiB, so the pipe is created with that size
#define CAPACITY 4096
#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_SET 0xDEAD
#define WAIT_TIMEOUT_US 6000000
#define MAX_WAITERS 3
// The byte the receive buffers hold until something writes them
#define MARKER '-'

typedef struct Waiter {
    SceUInt32 mode;
    int size;
    char *buffer;
    SceUID thread;
    volatile int result;
    SceSize moved;
    SceUInt32 timeout;
} Waiter;

// What a waiter shows when it is read: whether it still waits, the size it reports and its buffer
typedef struct Seen {
    int waits;
    SceSize moved;
    char buffer[16];
} Seen;

static SceUID pipe;
static Waiter waiters[MAX_WAITERS];
static char bufs[MAX_WAITERS][16];
static char big[CAPACITY], pattern[CAPACITY], drained[CAPACITY];
static char tail[16];

static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    Waiter *w = &waiters[i];
    w->result = sceKernelReceiveMsgPipe(pipe, w->buffer, w->size, w->mode, &w->moved, &w->timeout);
    return 0;
}

// Starts waiter i, a receive of size bytes in mode into buffer, and returns once it waits
static void start_waiter(int i, SceUInt32 mode, int size, SceUInt32 priority, char *buffer) {
    Waiter *w = &waiters[i];
    w->mode = mode;
    w->size = size;
    w->buffer = buffer;
    w->result = NOT_SET;
    w->moved = NOT_SET;
    w->timeout = WAIT_TIMEOUT_US;
    sceClibMemset(buffer, MARKER, 8);
    buffer[8] = '\0';
    w->thread = sceKernelCreateThread("waiter", waiter_entry, priority, 0x4000, 0,
                                      SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w->thread, sizeof(i), &i);
    test_wait_until_waiting(w->thread);
}

static SceKernelMppInfo pipe_info(void) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    return info;
}

// Sends size bytes of data in mode, and returns the result. The size goes to *moved.
static int send(const char *data, int size, SceUInt32 mode, SceSize *moved) {
    SceUInt32 timeout = WAIT_TIMEOUT_US;
    *moved = NOT_SET;
    return sceKernelSendMsgPipe(pipe, (void *)data, size, mode, moved, &timeout);
}

// Receives size bytes in mode, and returns the result. The size goes to *moved.
static int receive(char *buffer, int size, SceUInt32 mode, SceSize *moved) {
    SceUInt32 timeout = WAIT_TIMEOUT_US;
    *moved = NOT_SET;
    return sceKernelReceiveMsgPipe(pipe, buffer, size, mode, moved, &timeout);
}

// Reads waiter i. A woken waiter may already have run, so only whether it still waits is kept.
static Seen look(int i) {
    Seen seen = { 0 };
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(waiters[i].thread, &info);
    seen.waits = (info.status & SCE_THREAD_WAITING) != 0;
    seen.moved = waiters[i].moved;
    sceClibMemcpy(seen.buffer, waiters[i].buffer, sizeof(seen.buffer) - 1);
    return seen;
}

static void join_waiter(int i) {
    test_join_thread(waiters[i].thread);
}

static void print_seen(const char *name, int i, const Seen *seen) {
    PRINTF(seen->waits, "%s: R%d still waits right after the send", name, i + 1);
    PRINTF(seen->moved, "%s: R%d's size right after the send", name, i + 1);
    PRINTF(sceClibStrnlen(seen->buffer, 16), "%s: R%d's buffer \"%s\" after the send", name, i + 1,
           seen->buffer);
}

static void print_result(const char *name, int i) {
    const Waiter *w = &waiters[i];
    PRINTF(w->result, "%s: R%d's result", name, i + 1);
    PRINTF(w->moved, "%s: R%d's size", name, i + 1);
    PRINTF(sceClibStrnlen(w->buffer, 16), "%s: R%d's buffer \"%s\"", name, i + 1, w->buffer);
}

// R1 FULL 4, R2 ASAP 4 and R3 FULL 8 wait in that order on a FIFO pipe
static void fifo_three(void) {
    const char *name = "FIFO three";
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0x0, CAPACITY, NULL);
    start_waiter(0, MSG_PIPE_MODE_FULL, 4, WAITER_PRIORITY, bufs[0]);
    start_waiter(1, MSG_PIPE_MODE_ASAP, 4, WAITER_PRIORITY, bufs[1]);
    start_waiter(2, MSG_PIPE_MODE_FULL, 8, WAITER_PRIORITY, bufs[2]);

    SceSize sent = NOT_SET;
    const int send_result = send("ABCDEFGHIJ", 10, MSG_PIPE_MODE_FULL, &sent);
    const SceKernelMppInfo info = pipe_info();
    const Seen seen[MAX_WAITERS] = { look(0), look(1), look(2) };
    join_waiter(0);
    join_waiter(1);

    PRINTF(send_result, "%s: the send", name);
    PRINTF(sent, "%s: the send's size", name);
    PRINTF(info.freeSize, "%s: the free size after the send", name);
    PRINTF(info.numReceiveWaitThreads, "%s: the waiting receivers after the send", name);
    for (int i = 0; i < MAX_WAITERS; i++)
        print_seen(name, i, &seen[i]);
    print_result(name, 0);
    print_result(name, 1);

    // R3 still waits for 6 bytes, which the second send completes
    const int second_result = send("KLMNOP", 6, MSG_PIPE_MODE_FULL, &sent);
    join_waiter(2);
    const SceKernelMppInfo ended = pipe_info();
    PRINTF(second_result, "%s: the second send", name);
    PRINTF(sent, "%s: the second send's size", name);
    print_result(name, 2);
    PRINTF(ended.freeSize, "%s: the free size after R3 ended", name);
    PRINTF(ended.numReceiveWaitThreads, "%s: the waiting receivers after R3 ended", name);
    sceKernelCancelMsgPipe(pipe, NULL, NULL);
    sceKernelDeleteMsgPipe(pipe);
}

// R3 FULL 8, R2 ASAP 4 and R1 FULL 4 wait, with R3 highest and R1 lowest, on a pipe that queues
// receivers by priority
static void priority_three(void) {
    const char *name = "priority three";
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, MSG_PIPE_ATTR_RECEIVE_PRIO, CAPACITY,
                                  NULL);
    start_waiter(0, MSG_PIPE_MODE_FULL, 4, 0xA8, bufs[0]);
    start_waiter(1, MSG_PIPE_MODE_ASAP, 4, 0xA0, bufs[1]);
    start_waiter(2, MSG_PIPE_MODE_FULL, 8, 0x98, bufs[2]);

    SceSize sent = NOT_SET;
    const int send_result = send("ABCDEFGHIJ", 10, MSG_PIPE_MODE_FULL, &sent);
    const SceKernelMppInfo info = pipe_info();
    const Seen seen[MAX_WAITERS] = { look(0), look(1), look(2) };
    join_waiter(2);
    join_waiter(1);
    const int r1_waits = test_in_wait(waiters[0].thread, WAIT_TYPE_MSG_PIPE, pipe);

    PRINTF(send_result, "%s: the send", name);
    PRINTF(sent, "%s: the send's size", name);
    PRINTF(info.freeSize, "%s: the free size after the send", name);
    PRINTF(info.numReceiveWaitThreads, "%s: the waiting receivers after the send", name);
    PRINTF(r1_waits, "%s: R1 still waits", name);
    for (int i = 0; i < MAX_WAITERS; i++)
        print_seen(name, i, &seen[i]);
    print_result(name, 2);
    print_result(name, 1);

    // R1 is the one left, so the send of 4 completes it
    const int second_result = send("KLMN", 4, MSG_PIPE_MODE_FULL, &sent);
    join_waiter(0);
    PRINTF(second_result, "%s: the second send", name);
    PRINTF(sent, "%s: the second send's size", name);
    print_result(name, 0);
    sceKernelCancelMsgPipe(pipe, NULL, NULL);
    sceKernelDeleteMsgPipe(pipe);
}

// Two ASAP receivers of 2 wait, and a FULL send of 6 gives each 2 bytes, leaving 2 in the pipe
static void asap_pair(void) {
    const char *name = "ASAP 2 and 2";
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0x0, CAPACITY, NULL);
    start_waiter(0, MSG_PIPE_MODE_ASAP, 2, WAITER_PRIORITY, bufs[0]);
    start_waiter(1, MSG_PIPE_MODE_ASAP, 2, WAITER_PRIORITY, bufs[1]);

    SceSize sent = NOT_SET;
    const int send_result = send("ABCDEF", 6, MSG_PIPE_MODE_FULL, &sent);
    const SceKernelMppInfo info = pipe_info();
    const Seen seen[2] = { look(0), look(1) };
    join_waiter(0);
    join_waiter(1);

    PRINTF(send_result, "%s: the send", name);
    PRINTF(sent, "%s: the send's size", name);
    PRINTF(info.freeSize, "%s: the free size after the send", name);
    PRINTF(info.numReceiveWaitThreads, "%s: the waiting receivers after the send", name);
    for (int i = 0; i < 2; i++)
        print_seen(name, i, &seen[i]);
    print_result(name, 0);
    print_result(name, 1);

    SceSize got = NOT_SET;
    sceClibMemset(tail, 0, sizeof(tail));
    const int drain_result = receive(tail, 2, MSG_PIPE_MODE_FULL, &got);
    PRINTF(drain_result, "%s: the drain", name);
    PRINTF(got, "%s: the drain's size", name);
    PRINTF(sceClibStrnlen(tail, 16), "%s: the drain's bytes \"%s\"", name, tail);
    sceKernelCancelMsgPipe(pipe, NULL, NULL);
    sceKernelDeleteMsgPipe(pipe);
}

// FULL 4 waits, and a FULL send of 4096 gives it 4 bytes and puts the other 4092 into the pipe
static void ring_then_head(void) {
    const char *name = "FULL 4, send 4096";
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0x0, CAPACITY, NULL);
    start_waiter(0, MSG_PIPE_MODE_FULL, 4, WAITER_PRIORITY, bufs[0]);

    SceSize sent = NOT_SET;
    const int send_result = send(pattern, CAPACITY, MSG_PIPE_MODE_FULL, &sent);
    const SceKernelMppInfo info = pipe_info();
    const Seen seen = look(0);
    join_waiter(0);

    PRINTF(send_result, "%s: the send", name);
    PRINTF(sent, "%s: the send's size", name);
    PRINTF(info.freeSize, "%s: the free size after the send", name);
    PRINTF(info.numReceiveWaitThreads, "%s: the waiting receivers after the send", name);
    print_seen(name, 0, &seen);
    print_result(name, 0);
    PRINTF(sceClibMemcmp(bufs[0], pattern, 4) == 0, "%s: R1 got the first 4 bytes", name);

    SceSize got = NOT_SET;
    const int drain_result = receive(drained, CAPACITY - 4, MSG_PIPE_MODE_FULL, &got);
    PRINTF(drain_result, "%s: the drain", name);
    PRINTF(got, "%s: the drain's size", name);
    PRINTF(sceClibMemcmp(drained, pattern + 4, CAPACITY - 4) == 0, "%s: the drain holds the rest",
           name);
    const SceKernelMppInfo ended = pipe_info();
    PRINTF(ended.freeSize, "%s: the free size after the drain", name);
    sceKernelCancelMsgPipe(pipe, NULL, NULL);
    sceKernelDeleteMsgPipe(pipe);
}

// FULL 4096 waits on an empty pipe, and a FULL send of 4096 hands it all the bytes
static void head_takes_all(void) {
    const char *name = "FULL 4096, send 4096";
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0x0, CAPACITY, NULL);
    start_waiter(0, MSG_PIPE_MODE_FULL, CAPACITY, WAITER_PRIORITY, big);

    SceSize sent = NOT_SET;
    const int send_result = send(pattern, CAPACITY, MSG_PIPE_MODE_FULL, &sent);
    const SceKernelMppInfo info = pipe_info();
    const int waits = look(0).waits;
    join_waiter(0);

    PRINTF(send_result, "%s: the send", name);
    PRINTF(sent, "%s: the send's size", name);
    PRINTF(info.freeSize, "%s: the free size after the send", name);
    PRINTF(info.numReceiveWaitThreads, "%s: the waiting receivers after the send", name);
    PRINTF(waits, "%s: R1 still waits right after the send", name);
    PRINTF(waiters[0].result, "%s: R1's result", name);
    PRINTF(waiters[0].moved, "%s: R1's size", name);
    PRINTF(sceClibMemcmp(big, pattern, CAPACITY) == 0, "%s: R1's bytes are the pattern", name);
    sceKernelCancelMsgPipe(pipe, NULL, NULL);
    sceKernelDeleteMsgPipe(pipe);
}

// FULL 8 and ASAP 4 wait, and an ASAP send of 6 gives FULL 8 all 6 bytes and ASAP 4 nothing
static void asap_send_partial(void) {
    const char *name = "ASAP 6, FULL 8, ASAP 4";
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0x0, CAPACITY, NULL);
    start_waiter(0, MSG_PIPE_MODE_FULL, 8, WAITER_PRIORITY, bufs[0]);
    start_waiter(1, MSG_PIPE_MODE_ASAP, 4, WAITER_PRIORITY, bufs[1]);

    // Nothing is woken by this send, so every read below sees both receivers blocked
    SceSize sent = NOT_SET;
    const int send_result = send("ABCDEF", 6, MSG_PIPE_MODE_ASAP, &sent);
    const SceKernelMppInfo info = pipe_info();
    const Seen seen[2] = { look(0), look(1) };

    PRINTF(send_result, "%s: the ASAP send of 6", name);
    PRINTF(sent, "%s: the send's size", name);
    PRINTF(info.freeSize, "%s: the free size after the send", name);
    PRINTF(info.numReceiveWaitThreads, "%s: the waiting receivers after the send", name);
    for (int i = 0; i < 2; i++)
        print_seen(name, i, &seen[i]);

    // A send of 2 completes R1, and a send of 1 completes R2
    SceSize second = NOT_SET;
    const int second_result = send("GH", 2, MSG_PIPE_MODE_FULL, &second);
    join_waiter(0);
    PRINTF(second_result, "%s: the send of 2", name);
    PRINTF(second, "%s: the send of 2's size", name);
    print_result(name, 0);
    PRINTF(test_in_wait(waiters[1].thread, WAIT_TYPE_MSG_PIPE, pipe), "%s: R2 still waits", name);

    SceSize third = NOT_SET;
    const int third_result = send("I", 1, MSG_PIPE_MODE_FULL, &third);
    join_waiter(1);
    PRINTF(third_result, "%s: the send of 1", name);
    PRINTF(third, "%s: the send of 1's size", name);
    print_result(name, 1);
    sceKernelCancelMsgPipe(pipe, NULL, NULL);
    sceKernelDeleteMsgPipe(pipe);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);
    for (int i = 0; i < CAPACITY; i++)
        pattern[i] = 'a' + i % 26;

    fifo_three();
    priority_three();
    asap_pair();
    ring_then_head();
    head_takes_all();
    asap_send_partial();

    test_exit();
    return 0;
}
