// Where a new message pipe receive queues when an event waiter is at the front of the queue and a
// receive that holds part of its bytes is behind it. The pipe has attr 0x2, which queues receivers
// by priority, 4096 bytes, and starts empty. Every thread but the main one is pinned to core 0, and
// the main thread runs on core 1. R1, at 0xA0, waits for a FULL receive of 8. The main thread sends
// "abcd" with a try send, so R1 holds 4 of its 8. Then E, at 0x50, waits with sceKernelWaitEvent on
// the pipe for the error bit 0x10, which nothing posts until the end. By priority E belongs in
// front of R1, and it is meant to take the front of the queue. Then R2, at 0x80, waits for a FULL
// receive of 4. The main thread try sends "efgh", and once one receive ended, "ijkl". Last, a try
// receive on the empty pipe fails and posts the error bit for E. The test prints whether each
// thread waits at each step, R1's size after the first send, the pipe's info while all three wait,
// which receive ended after the second send, per receive its result, what it got and its size, and
// E's result, bits and user data. The question is whether R2 goes in front of R1, which gives R2
// "efgh", or behind it, which gives R2 "ijkl". The receives skip E, so the test cannot see E's
// place directly. If R2 gets "ijkl", it stays open whether E took the front or queued behind R1.
//
// On a PS Vita TV on firmware 3.65 R2 goes in front of R1. R2 gets "efgh" and R1 ends with
// "abcdijkl". The pipe info counts E with the waiting receivers.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define R1_PRIORITY 0xA0
#define E_PRIORITY 0x50
#define R2_PRIORITY 0x80
#define ERROR_BIT 0x10
#define LONG_TIMEOUT (3 * TEST_WAIT_US)
#define NOT_DONE 0xDEAD

static SceUID pipe;
static volatile int done_count, r1_done, r2_done, e_done;
static volatile int r1_result = NOT_DONE, r2_result = NOT_DONE, e_result = NOT_DONE;
static volatile SceSize r1_size = NOT_DONE, r2_size = NOT_DONE;
static SceUInt32 e_bits = NOT_DONE;
static SceUInt64 e_user_data;
static char r1_buf[16], r2_buf[16];

static int r1_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = LONG_TIMEOUT;
    r1_result = sceKernelReceiveMsgPipe(pipe, r1_buf, 8, MSG_PIPE_MODE_FULL, (SceSize *)&r1_size, &timeout);
    r1_done = 1;
    __atomic_fetch_add(&done_count, 1, __ATOMIC_SEQ_CST);
    return 0;
}

static int r2_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = LONG_TIMEOUT;
    r2_result = sceKernelReceiveMsgPipe(pipe, r2_buf, 4, MSG_PIPE_MODE_FULL, (SceSize *)&r2_size, &timeout);
    r2_done = 1;
    __atomic_fetch_add(&done_count, 1, __ATOMIC_SEQ_CST);
    return 0;
}

static int e_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = LONG_TIMEOUT;
    e_result = sceKernelWaitEvent(pipe, ERROR_BIT, &e_bits, &e_user_data, &timeout);
    e_done = 1;
    return 0;
}

// Starts entry on core 0 at priority and waits until it waits
static SceUID start(SceKernelThreadEntry entry, int priority) {
    const SceUID thread = sceKernelCreateThread("thread", entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    test_wait_until_waiting(thread);
    return thread;
}

static int send(const char *bytes) {
    SceSize size;
    return sceKernelTrySendMsgPipe(pipe, (void *)bytes, 4, MSG_PIPE_MODE_FULL, &size);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);

    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, MSG_PIPE_ATTR_RECEIVE_PRIO, CAPACITY, NULL);

    const SceUID r1 = start(r1_entry, R1_PRIORITY);
    const int r1_waits = test_in_wait(r1, WAIT_TYPE_MSG_PIPE, pipe);
    const int first_send = send("abcd");
    const int r1_size_after_first = r1_size;
    const int r1_done_after_first = r1_done;

    const SceUID e = start(e_entry, E_PRIORITY);
    const int e_waits = test_in_wait(e, WAIT_TYPE_EVENT, pipe);
    const SceUID r2 = start(r2_entry, R2_PRIORITY);
    const int r2_waits = test_in_wait(r2, WAIT_TYPE_MSG_PIPE, pipe);
    const int r1_still_waits = test_in_wait(r1, WAIT_TYPE_MSG_PIPE, pipe);
    SceKernelMppInfo all = { .size = sizeof(all) };
    sceKernelGetMsgPipeInfo(pipe, &all);

    const int second_send = send("efgh");
    TEST_WAIT_FOR(done_count >= 1);
    const int r1_done_after_second = r1_done;
    const int r2_done_after_second = r2_done;
    const int e_waits_after_second = test_in_wait(e, WAIT_TYPE_EVENT, pipe);
    const int third_send = send("ijkl");
    TEST_WAIT_FOR(done_count >= 2);
    const int e_waits_after_third = test_in_wait(e, WAIT_TYPE_EVENT, pipe);
    // Read before the cancel, which empties the pipe, so bytes left behind show
    SceKernelMppInfo after = { .size = sizeof(after) };
    sceKernelGetMsgPipeInfo(pipe, &after);

    // A try receive on the empty pipe fails, which posts the error bit E waits for
    char none[4];
    const int failed_receive = sceKernelTryReceiveMsgPipe(pipe, none, 4, MSG_PIPE_MODE_FULL, NULL);
    TEST_WAIT_FOR(e_done);
    // Ends a wait that still goes on, if any, so the joins return
    sceKernelCancelMsgPipe(pipe, NULL, NULL);
    test_join_thread(r1);
    test_join_thread(r2);
    test_join_thread(e);
    sceKernelDeleteMsgPipe(pipe);

    PRINT("R1 waits before the first send", r1_waits);
    PRINT("the try send of \"abcd\"", first_send);
    PRINT("R1's size after the send of \"abcd\"", r1_size_after_first);
    PRINT("R1 done after the send of \"abcd\"", r1_done_after_first);
    PRINT("E waits on the pipe", e_waits);
    PRINT("R2 waits on the pipe", r2_waits);
    PRINT("R1 still waits then", r1_still_waits);
    PRINT("all three wait: the attr", all.attr);
    PRINT("all three wait: the free size", all.freeSize);
    PRINT("all three wait: the waiting senders", all.numSendWaitThreads);
    PRINT("all three wait: the waiting receivers", all.numReceiveWaitThreads);
    PRINT("the try send of \"efgh\"", second_send);
    PRINT("R1 done after the send of \"efgh\"", r1_done_after_second);
    PRINT("R2 done after the send of \"efgh\"", r2_done_after_second);
    PRINT("E still waits after the send of \"efgh\"", e_waits_after_second);
    PRINT("the try send of \"ijkl\"", third_send);
    PRINT("E still waits after the send of \"ijkl\"", e_waits_after_third);
    PRINT("the bytes left in the pipe after both receives", CAPACITY - after.freeSize);
    PRINT("R1's result", r1_result);
    PRINTF(r1_size, "R1 got \"%s\", size", r1_buf);
    PRINT("R2's result", r2_result);
    PRINTF(r2_size, "R2 got \"%s\", size", r2_buf);
    PRINT("the failed try receive", failed_receive);
    PRINT("E's result", e_result);
    PRINT("E's bits", e_bits);
    PRINT("E's user data, low half", (SceUInt32)e_user_data);
    PRINT("E's user data, high half", (SceUInt32)(e_user_data >> 32));
    test_exit();
    return 0;
}
