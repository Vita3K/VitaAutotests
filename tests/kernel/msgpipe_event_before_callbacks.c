// What a callback sees of a message pipe's event when it runs at the end of a send or a receive with
// callbacks on that pipe. A thread T creates a callback C, notifies it, and calls a send or a
// receive with callbacks that ends at once. C runs at the start of the call, and its first run
// notifies C again, so C runs again at the end of the call, where a send or a receive with
// callbacks checks them on every exit. That second run reads the pipe's event bits and user data.
// Every case has a fresh pipe of 0x1000 bytes whose bits were cleared right before the call:
//
// - a send of 4 bytes, FULL, into an empty pipe
// - a receive of 4 bytes, FULL, from a pipe holding 4
//
// Each case prints the call's result, how often C ran, the bits and both halves of the user data
// that C's second run read, and the bits after the call.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 0x1000
#define MESSAGE_SIZE 4

typedef enum Kind { SEND, RECEIVE } Kind;

static Kind kind;
static SceUID pipe, callback;
static char buffer[MESSAGE_SIZE];
static volatile int callback_runs, call_result = -1;
static volatile SceUInt32 seen_bits = 0xDEAD;
static volatile SceUInt64 seen_user_data;

// Reads the pipe's event bits and user data
static void read_event(SceUInt32 *bits, SceUInt64 *user_data) {
    EventInfo info = { .size = sizeof(info) };
    *bits = 0xDEAD;
    *user_data = 0;
    if (sceKernelGetEventInfo(pipe, &info) == 0) {
        *bits = info.eventPattern;
        *user_data = info.userData;
    }
}

// Notifies itself on its first run, and reads the pipe's event on its second
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    if (++callback_runs == 1) {
        sceKernelNotifyCallback(callback, 2);
        return 0;
    }
    SceUInt32 bits;
    SceUInt64 user_data;
    read_event(&bits, &user_data);
    seen_bits = bits;
    seen_user_data = user_data;
    return 0;
}

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("cb", 0, callback_entry, NULL);
    sceKernelNotifyCallback(callback, 1);
    SceUInt32 timeout = TEST_WAIT_US;
    SceSize size = 0;
    if (kind == SEND)
        call_result = sceKernelSendMsgPipeCB(pipe, buffer, sizeof(buffer), MSG_PIPE_MODE_FULL, &size, &timeout);
    else
        call_result = sceKernelReceiveMsgPipeCB(pipe, buffer, sizeof(buffer), MSG_PIPE_MODE_FULL, &size, &timeout);
    return 0;
}

static void run(const char *name, Kind new_kind) {
    kind = new_kind;
    callback_runs = 0;
    call_result = -1;
    seen_bits = 0xDEAD;
    seen_user_data = 0;
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    if (kind == RECEIVE)
        sceKernelTrySendMsgPipe(pipe, buffer, sizeof(buffer), MSG_PIPE_MODE_FULL, NULL);
    sceKernelClearEvent(pipe, 0);
    test_join_thread(test_start_thread(t_entry));
    PRINTF(call_result, "%s: the call", name);
    PRINTF(callback_runs, "%s: runs of the callback", name);
    PRINTF(seen_bits, "%s: the bits the second run saw", name);
    PRINTF((SceUInt32)seen_user_data, "%s: the low half of the user data it saw", name);
    PRINTF((SceUInt32)(seen_user_data >> 32), "%s: the high half of the user data it saw", name);
    SceUInt32 bits;
    SceUInt64 user_data;
    read_event(&bits, &user_data);
    PRINTF(bits, "%s: the bits after the call", name);
    sceKernelDeleteMsgPipe(pipe);
}

int main(void) {
    run("send", SEND);
    run("receive", RECEIVE);
    test_exit();
    return 0;
}
