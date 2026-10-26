// What auto reset does to the bit 0x10000, which a message pipe sets while a send left bytes in it.
// Every case has a fresh pipe of 0x1000 bytes with the auto reset attribute, its bits cleared, then
// a send of 4 bytes, so the pipe holds the bits 0x10001. The cases:
//
// - two polls for 0x10000, then two polls for 0x1
// - a thread waiting for 0x10000 with sceKernelWaitEvent when the send comes
// - a thread in an OR multiple event wait for 0x10000 on the pipe when the send comes
// - an OR multiple event wait for 0x10000 after the send, whose scan finds the bit
//
// Each case prints what every poll or wait returns and the bits it got, and the pipe's bits after
// each step.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 0x1000
#define MESSAGE_SIZE 4
#define DATA_EXIST 0x10000

typedef enum Kind { EVENT_WAIT, MULTIPLE_WAIT } Kind;

static Kind kind;
static SceUID pipe;
static char buffer[MESSAGE_SIZE];
static volatile int w_ready, w_result = -1;
static volatile SceUInt32 w_bits = 0xDEAD;

// The pipe's event bits
static SceUInt32 bits_of(void) {
    EventInfo info = { .size = sizeof(info) };
    return sceKernelGetEventInfo(pipe, &info) == 0 ? info.eventPattern : 0xDEAD;
}

// An OR multiple event wait for 0x10000 on the pipe. Returns the wait's result and its first
// entry's bits.
static int wait_multiple(SceUInt32 *bits) {
    WaitEvent events[1] = { { pipe, DATA_EXIST } };
    ResultEvent results[1] = { { 0, 0, 0xDEAD, 0, 0 } };
    SceUInt32 timeout = TEST_WAIT_US;
    const int result = sceKernelWaitMultipleEvents(events, 1, EVENT_WAIT_MODE_OR, results, &timeout);
    *bits = results[0].resultPattern;
    return result;
}

// Waits for 0x10000 on the pipe, with sceKernelWaitEvent or a multiple event wait
static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    w_ready = 1;
    SceUInt32 bits = 0xDEAD;
    if (kind == EVENT_WAIT) {
        SceUInt64 user_data = 0;
        SceUInt32 timeout = TEST_WAIT_US;
        w_result = sceKernelWaitEvent(pipe, DATA_EXIST, &bits, &user_data, &timeout);
    } else {
        w_result = wait_multiple(&bits);
    }
    w_bits = bits;
    return 0;
}

static void new_pipe(void) {
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, EVENT_ATTR_AUTO_RESET, CAPACITY, NULL);
    sceKernelClearEvent(pipe, 0);
}

static void send(void) {
    sceKernelTrySendMsgPipe(pipe, buffer, sizeof(buffer), MSG_PIPE_MODE_FULL, NULL);
}

// Polls for bit, then prints the result, the bits it got and the pipe's bits after
static void poll(const char *name, SceUInt32 bit) {
    SceUInt32 got = 0xDEAD;
    SceUInt64 user_data = 0;
    PRINTF(sceKernelPollEvent(pipe, bit, &got, &user_data), "%s: the poll", name);
    PRINTF(got, "%s: the bits it got", name);
    PRINTF(bits_of(), "%s: the bits after", name);
}

// A thread waits for 0x10000 when the send comes
static void waiter_case(const char *name, Kind new_kind) {
    kind = new_kind;
    w_ready = 0;
    w_result = -1;
    w_bits = 0xDEAD;
    new_pipe();
    const SceUID w = test_start_thread(w_entry);
    TEST_WAIT_FOR(w_ready);
    test_wait_until_waiting(w);
    send();
    test_join_thread(w);
    PRINTF(w_result, "%s: the wait", name);
    PRINTF(w_bits, "%s: the bits it got", name);
    PRINTF(bits_of(), "%s: the bits after", name);
    sceKernelDeleteMsgPipe(pipe);
}

int main(void) {
    new_pipe();
    send();
    PRINT("polls: the bits after the send", bits_of());
    poll("polls: the first poll for 0x10000", DATA_EXIST);
    poll("polls: the second poll for 0x10000", DATA_EXIST);
    poll("polls: the first poll for 0x1", EVENT_IN);
    poll("polls: the second poll for 0x1", EVENT_IN);
    sceKernelDeleteMsgPipe(pipe);

    waiter_case("event wait", EVENT_WAIT);
    waiter_case("multiple event wait", MULTIPLE_WAIT);

    new_pipe();
    send();
    SceUInt32 bits = 0xDEAD;
    PRINT("multiple event wait after the send: the wait", wait_multiple(&bits));
    PRINT("multiple event wait after the send: the bits it got", bits);
    PRINT("multiple event wait after the send: the bits after", bits_of());
    sceKernelDeleteMsgPipe(pipe);

    test_exit();
    return 0;
}
