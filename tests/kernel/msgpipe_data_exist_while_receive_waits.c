// Whether a message pipe still reports the data exist bit 0x10000 while a blocking receive that
// took its bytes goes on waiting. The pipe has attr 0 and 4096 bytes. R is pinned to core 0 at
// 0x80, and the main thread runs on core 1. The main thread sends "abcd" with a try send, which
// sets the bit. Then R waits for a FULL receive of 8, which takes the 4 bytes and waits for 4 more.
// While R waits, the main thread try sends "efgh", which completes R. The test records three times,
// after the first send, while R waits and after the second send and R's end: a sceKernelPollEvent
// on the pipe for 0x10000 with its result and the bits it gives, the result of
// sceKernelGetEventInfo on the pipe and its pattern, and the pipe's free size. It also prints
// whether R waits, and R's result, what it got and its size.
//
// On a PS Vita TV on firmware 3.65 the bit is clear while R waits: once R took the pipe's 4 bytes
// the poll for 0x10000 gives 0x80028202 and the pattern is 0x5. It stays clear after R's end.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define R_PRIORITY 0x80
#define DATA_EXIST 0x10000
#define LONG_TIMEOUT (3 * TEST_WAIT_US)
#define NOT_DONE 0xDEAD

typedef struct Snapshot {
    int poll;
    SceUInt32 poll_bits;
    int info;
    SceUInt32 pattern;
    SceSize free_size;
} Snapshot;

static SceUID pipe;
static volatile int r_done;
static volatile int r_result = NOT_DONE;
static volatile SceSize r_size = NOT_DONE;
static char r_buf[16];

static int r_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = LONG_TIMEOUT;
    r_result = sceKernelReceiveMsgPipe(pipe, r_buf, 8, MSG_PIPE_MODE_FULL, (SceSize *)&r_size, &timeout);
    __atomic_store_n(&r_done, 1, __ATOMIC_RELEASE);
    return 0;
}

static int send(const char *bytes) {
    SceSize size;
    return sceKernelTrySendMsgPipe(pipe, (void *)bytes, 4, MSG_PIPE_MODE_FULL, &size);
}

// Records the data exist bit by poll, the pattern by the info call, and the free size
static Snapshot snapshot(void) {
    Snapshot s = { .poll_bits = NOT_DONE, .pattern = NOT_DONE };
    SceUInt64 user_data;
    s.poll = sceKernelPollEvent(pipe, DATA_EXIST, &s.poll_bits, &user_data);
    EventInfo event = { .size = sizeof(event) };
    s.info = sceKernelGetEventInfo(pipe, &event);
    if (s.info == 0)
        s.pattern = event.eventPattern;
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    s.free_size = info.freeSize;
    return s;
}

static void print_snapshot(const char *when, const Snapshot *s) {
    PRINTF(s->poll, "%s: the poll for 0x10000", when);
    PRINTF(s->poll_bits, "%s: the poll's bits", when);
    PRINTF(s->info, "%s: the event info", when);
    PRINTF(s->pattern, "%s: the pattern", when);
    PRINTF(s->free_size, "%s: the free size", when);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);

    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    const int first_send = send("abcd");
    const Snapshot before = snapshot();

    const SceUID r = sceKernelCreateThread("thread", r_entry, R_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(r, 0, NULL);
    TEST_WAIT_FOR(__atomic_load_n(&r_done, __ATOMIC_ACQUIRE) || test_in_wait(r, WAIT_TYPE_MSG_PIPE, pipe));
    const int r_waits = test_in_wait(r, WAIT_TYPE_MSG_PIPE, pipe);
    const int r_size_while_waiting = r_size;
    const Snapshot waiting = snapshot();

    const int second_send = send("efgh");
    TEST_WAIT_FOR(__atomic_load_n(&r_done, __ATOMIC_ACQUIRE));
    const Snapshot after = snapshot();
    // Ends a wait that still goes on, if any, so the join returns
    sceKernelCancelMsgPipe(pipe, NULL, NULL);
    test_join_thread(r);
    sceKernelDeleteMsgPipe(pipe);

    PRINT("the try send of \"abcd\"", first_send);
    print_snapshot("after the send of \"abcd\"", &before);
    PRINT("R waits on the pipe", r_waits);
    PRINT("R's size while it waits", r_size_while_waiting);
    print_snapshot("while R waits", &waiting);
    PRINT("the try send of \"efgh\"", second_send);
    print_snapshot("after the send of \"efgh\" and R's end", &after);
    PRINT("R's result", r_result);
    PRINTF(r_size, "R got \"%s\", size", r_buf);
    test_exit();
    return 0;
}
