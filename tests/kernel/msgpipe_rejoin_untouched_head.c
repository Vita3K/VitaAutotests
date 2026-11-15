// Where a message pipe receive that already holds part of its bytes queues again after a callback,
// when a receive of lower priority that moved nothing queued while it was out. The pipe has attr
// 0x2, which queues receivers by priority, 4096 bytes, and starts empty. Every thread but the main
// one is pinned to core 0, and the main thread runs on core 1. R, at 0x80, waits with
// sceKernelReceiveMsgPipeCB for a FULL receive of 8. The main thread sends "abcd" with a try send,
// so R holds 4 of its 8. Then the main thread notifies R's callback, which blocks on a gate until
// the main thread opens it. While R runs its callback, U, at 0xA0, waits with a plain FULL receive
// of 4, which moves nothing. Then the gate opens and R's wait goes on. Once R waits on the pipe
// again, the main thread try sends "efgh", and once one receive ended, "ijkl". The test prints
// whether each thread waits at each step, R's size after the first send, the pipe's free size and
// waiting receivers while both wait, and per receive its result, what it got and its size. The
// question is whether R goes back in front of U, which then gets "ijkl", or behind it, which gives
// U "efgh".
//
// On a PS Vita TV on firmware 3.65 R goes back behind U. U gets "efgh" and R ends with "abcdijkl".
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define R_PRIORITY 0x80
#define U_PRIORITY 0xA0
#define LONG_TIMEOUT (3 * TEST_WAIT_US)
#define NOT_DONE 0xDEAD

static SceUID pipe, gate, callback;
static volatile int callback_ready, callback_started, done_count;
static volatile int r_done, u_done;
static volatile int r_result = NOT_DONE, u_result = NOT_DONE;
static volatile SceSize r_size = NOT_DONE, u_size = NOT_DONE;
static char r_buf[16], u_buf[16];

// Runs inside R's wait and blocks on the gate until the main thread opens it
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_started = 1;
    SceUInt timeout = LONG_TIMEOUT;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

static int r_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    callback_ready = 1;
    SceUInt timeout = LONG_TIMEOUT;
    r_result = sceKernelReceiveMsgPipeCB(pipe, r_buf, 8, MSG_PIPE_MODE_FULL, (SceSize *)&r_size, &timeout);
    r_done = 1;
    __atomic_fetch_add(&done_count, 1, __ATOMIC_SEQ_CST);
    sceKernelDeleteCallback(callback);
    return 0;
}

static int u_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = LONG_TIMEOUT;
    u_result = sceKernelReceiveMsgPipe(pipe, u_buf, 4, MSG_PIPE_MODE_FULL, (SceSize *)&u_size, &timeout);
    u_done = 1;
    __atomic_fetch_add(&done_count, 1, __ATOMIC_SEQ_CST);
    return 0;
}

// Starts entry on core 0 at priority
static SceUID start(SceKernelThreadEntry entry, int priority) {
    const SceUID thread = sceKernelCreateThread("thread", entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

static int send(const char *bytes) {
    SceSize size;
    return sceKernelTrySendMsgPipe(pipe, (void *)bytes, 4, MSG_PIPE_MODE_FULL, &size);
}

static SceKernelMppInfo pipe_info(void) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    return info;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);

    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, MSG_PIPE_ATTR_RECEIVE_PRIO, CAPACITY, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);

    const SceUID r = start(r_entry, R_PRIORITY);
    TEST_WAIT_FOR(callback_ready && test_in_wait(r, WAIT_TYPE_MSG_PIPE | WAIT_TYPE_CB, pipe));
    const int r_waits = test_in_wait(r, WAIT_TYPE_MSG_PIPE | WAIT_TYPE_CB, pipe);
    const int first_send = send("abcd");
    const int r_size_after_first = r_size;
    const int r_done_after_first = r_done;

    const int notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(callback_started && test_in_wait(r, WAIT_TYPE_SEMA, gate));
    const int r_in_gate = test_in_wait(r, WAIT_TYPE_SEMA, gate);

    const SceUID u = start(u_entry, U_PRIORITY);
    TEST_WAIT_FOR(test_in_wait(u, WAIT_TYPE_MSG_PIPE, pipe));
    const int u_waits = test_in_wait(u, WAIT_TYPE_MSG_PIPE, pipe);
    const SceKernelMppInfo u_alone = pipe_info();

    sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(test_in_wait(r, WAIT_TYPE_MSG_PIPE | WAIT_TYPE_CB, pipe));
    const int r_back = test_in_wait(r, WAIT_TYPE_MSG_PIPE | WAIT_TYPE_CB, pipe);
    const int u_still_waits = test_in_wait(u, WAIT_TYPE_MSG_PIPE, pipe);
    const SceKernelMppInfo both = pipe_info();

    const int second_send = send("efgh");
    TEST_WAIT_FOR(done_count >= 1);
    const int r_done_after_second = r_done;
    const int u_done_after_second = u_done;
    const int third_send = send("ijkl");
    TEST_WAIT_FOR(done_count >= 2);
    // Read before the cancel, which empties the pipe, so bytes left behind show
    const SceKernelMppInfo after = pipe_info();
    // Ends a wait that still goes on, if any, so the joins return
    sceKernelCancelMsgPipe(pipe, NULL, NULL);
    test_join_thread(r);
    test_join_thread(u);
    sceKernelDeleteMsgPipe(pipe);
    sceKernelDeleteSema(gate);

    PRINT("R waits with callbacks before the first send", r_waits);
    PRINT("the try send of \"abcd\"", first_send);
    PRINT("R's size after the send of \"abcd\"", r_size_after_first);
    PRINT("R done after the send of \"abcd\"", r_done_after_first);
    PRINT("the notify", notify);
    PRINT("R waits on the gate in its callback", r_in_gate);
    PRINT("U waits while R runs its callback", u_waits);
    PRINT("the free size with only U queued", u_alone.freeSize);
    PRINT("the waiting receivers with only U queued", u_alone.numReceiveWaitThreads);
    PRINT("R waits on the pipe again after the gate", r_back);
    PRINT("U still waits then", u_still_waits);
    PRINT("the free size with both queued", both.freeSize);
    PRINT("the waiting receivers with both queued", both.numReceiveWaitThreads);
    PRINT("the try send of \"efgh\"", second_send);
    PRINT("R done after the send of \"efgh\"", r_done_after_second);
    PRINT("U done after the send of \"efgh\"", u_done_after_second);
    PRINT("the try send of \"ijkl\"", third_send);
    PRINT("R's result", r_result);
    PRINTF(r_size, "R got \"%s\", size", r_buf);
    PRINT("U's result", u_result);
    PRINTF(u_size, "U got \"%s\", size", u_buf);
    PRINT("the bytes left in the pipe", CAPACITY - after.freeSize);
    test_exit();
    return 0;
}
