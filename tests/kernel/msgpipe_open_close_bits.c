// The event bits of a message pipe with the openable attribute: the open bit 0x100, which an open
// by name posts, the close bit 0x200, which closing an opened UID posts, and the delete bit 0x8,
// which deleting the creating UID posts while an opened UID keeps the pipe alive. The send bit 0x1
// is read after a send, with its user data. The test prints what each poll and wait returns, the
// bits it got, the pattern after each step, and for the user data of the open, close and delete two
// comparisons: whether the high word is the main thread's UID and whether the low word is the
// process ID. No UID is printed. Every case makes a fresh pipe, and the cases are:
//
// - a pipe that holds 4 bytes from a send. The test polls for the send bit, then opens the pipe by
//   name, reads the pattern, polls for the close bit, which isn't set yet, and polls for the open
//   bit. It then closes the opened UID and polls for the close bit.
// - a thread waiting for the open bit through the creating UID, which the main thread's open ends,
//   and the same for a thread waiting for the close bit, which the main thread's close ends.
// - a pipe opened twice and closed twice through the two opened UIDs, with the pattern after each
//   call.
// - the creating UID is deleted while an opened UID lives. A thread waiting for the delete bit on
//   the opened UID is woken, and the pipe still moves 4 bytes through the opened UID.
// - a pipe with the auto reset attribute, where a poll for the open bit and a poll for the close
//   bit each take the bit they match.
//
// Every wait has a timeout of 6 seconds, far past what the main thread needs to end it.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define OPEN_BIT 0x100
#define CLOSE_BIT 0x200
#define NOT_SET 0xDEAD
#define WAIT_TIMEOUT_US (3 * TEST_WAIT_US)

static SceUID main_thread, process;
static char pipe_name[32];
static SceUID pipe;
// The pipe a waiter waits on, and what its wait returned
static SceUID wait_on;
static SceUInt32 wait_pattern;
static SceUInt64 wait_user_data;
static int wait_result;

// Creates a fresh openable pipe under a name of its own, with the attributes attr added
static SceUID make_pipe(SceUInt32 attr) {
    static int made;
    sceClibSnprintf(pipe_name, sizeof(pipe_name), "OpenBits %d", ++made);
    return sceKernelCreateMsgPipe(pipe_name, MSG_PIPE_TYPE_USER, SCE_KERNEL_ATTR_OPENABLE | attr, CAPACITY, NULL);
}

// The pattern of an event object, or 0 if the call fails
static SceUInt32 pattern_of(SceUID id) {
    EventInfo info = { .size = sizeof(info) };
    sceKernelGetEventInfo(id, &info);
    return info.eventPattern;
}

// Prints whether the user data's high word is the main thread's UID and its low word the process ID
static void print_user_data(const char *name, SceUInt64 user_data) {
    PRINTF((SceUInt32)(user_data >> 32) == (SceUInt32)main_thread,
        "%s: the high word of the user data is the main thread's UID", name);
    PRINTF((SceUInt32)user_data == (SceUInt32)process,
        "%s: the low word of the user data is the process ID", name);
}

// Polls id for bit, printing the poll, and with detail also the bits it got and the user data
static void print_poll(const char *name, SceUID id, SceUInt32 bit, int detail) {
    SceUInt32 bits = 0;
    SceUInt64 user_data = 0;
    const int polled = sceKernelPollEvent(id, bit, &bits, &user_data);
    PRINTF(polled, "%s: poll for 0x%X", name, bit);
    if (polled < 0 || !detail)
        return;
    PRINTF(bits, "%s: poll for 0x%X, the bits it got", name, bit);
    print_user_data(name, user_data);
}

// Polls id for the send bit, printing the bits it got and the high and low words of the user data
static void print_send_poll(const char *name, SceUID id) {
    SceUInt32 bits = 0;
    SceUInt64 user_data = 0;
    PRINTF(sceKernelPollEvent(id, EVENT_IN, &bits, &user_data), "%s: poll for 0x1 after the send", name);
    PRINTF(bits, "%s: poll for 0x1 after the send, the bits it got", name);
    PRINTF((SceUInt32)(user_data >> 32), "%s: the high word of the send's user data", name);
    PRINTF((SceUInt32)user_data, "%s: the low word of the send's user data", name);
}

// Waits on wait_on for the bits in argp, and keeps what the wait returned
static int waiter(SceSize args, void *argp) {
    (void)args;
    const SceUInt32 bits = *(const SceUInt32 *)argp;
    SceUInt32 timeout = WAIT_TIMEOUT_US;
    SceUInt32 result = 0;
    SceUInt64 user_data = 0;
    wait_result = sceKernelWaitEvent(wait_on, bits, &result, &user_data, &timeout);
    wait_pattern = result;
    wait_user_data = user_data;
    return 0;
}

// A pipe that holds the bytes of a send, opened by name, then closed through the opened UID
static void run_open_close(const char *name) {
    pipe = make_pipe(0);
    PRINTF(test_created(pipe), "%s: create the pipe", name);
    SceUInt32 timeout = TEST_WAIT_US;
    SceSize sent = NOT_SET;
    PRINTF(sceKernelSendMsgPipe(pipe, "abcd", 4, MSG_PIPE_MODE_FULL, &sent, &timeout), "%s: send 4 bytes", name);
    PRINTF(sent, "%s: the send's size", name);
    print_send_poll(name, pipe);
    const SceUID opened = sceKernelOpenMsgPipe(pipe_name);
    PRINTF(test_created(opened), "%s: open it", name);
    PRINTF(pattern_of(pipe), "%s: the pattern after the open", name);
    print_poll(name, pipe, CLOSE_BIT, 1);
    print_poll(name, pipe, OPEN_BIT, 1);
    PRINTF(pattern_of(pipe), "%s: the pattern after the polls", name);
    PRINTF(sceKernelCloseMsgPipe(opened), "%s: close the opened UID", name);
    PRINTF(pattern_of(pipe), "%s: the pattern after the close", name);
    print_poll(name, pipe, CLOSE_BIT, 1);
    PRINTF(pattern_of(pipe), "%s: the pattern after the poll", name);
    sceKernelDeleteMsgPipe(pipe);
}

// A thread waiting for bit on the pipe, which the main thread's open of the pipe, or its close of
// the opened UID, is meant to end
static void run_wait(const char *name, SceUInt32 bit) {
    pipe = make_pipe(0);
    PRINTF(test_created(pipe), "%s: create the pipe", name);
    SceUID opened = -1;
    if (bit == CLOSE_BIT) {
        opened = sceKernelOpenMsgPipe(pipe_name);
        PRINTF(test_created(opened), "%s: open it first", name);
    }
    wait_on = pipe;
    wait_result = NOT_SET;
    wait_pattern = NOT_SET;
    wait_user_data = 0;
    const SceUID thread = test_start_thread_arg(waiter, bit);
    // With the pipe opened, the thread's info may report either of its UIDs as the one it waits on
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_EVENT, pipe) || (opened >= 0 && test_in_wait(thread, WAIT_TYPE_EVENT, opened)));
    if (bit == OPEN_BIT) {
        opened = sceKernelOpenMsgPipe(pipe_name);
        PRINTF(test_created(opened), "%s: open it", name);
    } else {
        PRINTF(sceKernelCloseMsgPipe(opened), "%s: close the opened UID", name);
    }
    test_join_thread(thread);
    PRINTF(wait_result, "%s: the wait", name);
    PRINTF(wait_pattern, "%s: the wait's result bits", name);
    print_user_data(name, wait_user_data);
    PRINTF(pattern_of(pipe), "%s: the pattern after", name);
    if (bit == OPEN_BIT)
        sceKernelCloseMsgPipe(opened);
    sceKernelDeleteMsgPipe(pipe);
}

// A pipe opened twice and closed twice through the two opened UIDs, with the pattern after each
// call
static void run_twice(const char *name) {
    pipe = make_pipe(0);
    PRINTF(test_created(pipe), "%s: create the pipe", name);
    SceUID opened[2];
    for (int i = 0; i < 2; i++) {
        opened[i] = sceKernelOpenMsgPipe(pipe_name);
        PRINTF(test_created(opened[i]), "%s: open number %d", name, i + 1);
        PRINTF(pattern_of(pipe), "%s: the pattern after open number %d", name, i + 1);
    }
    for (int i = 0; i < 2; i++) {
        PRINTF(sceKernelCloseMsgPipe(opened[i]), "%s: close number %d", name, i + 1);
        PRINTF(pattern_of(pipe), "%s: the pattern after close number %d", name, i + 1);
    }
    sceKernelDeleteMsgPipe(pipe);
}

// The creating UID is deleted while an opened UID lives. A thread waiting for the delete bit on the
// opened UID is woken by the delete, and the pipe still moves bytes through the opened UID.
static void run_delete(const char *name) {
    pipe = make_pipe(0);
    PRINTF(test_created(pipe), "%s: create the pipe", name);
    const SceUID opened = sceKernelOpenMsgPipe(pipe_name);
    PRINTF(test_created(opened), "%s: open it", name);
    wait_on = opened;
    wait_result = NOT_SET;
    wait_pattern = NOT_SET;
    wait_user_data = 0;
    const SceUID thread = test_start_thread_arg(waiter, EVENT_DELETE);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_EVENT, opened) || test_in_wait(thread, WAIT_TYPE_EVENT, pipe));
    PRINTF(sceKernelDeleteMsgPipe(pipe), "%s: delete the creating UID", name);
    test_join_thread(thread);
    PRINTF(wait_result, "%s: the wait for the delete bit", name);
    PRINTF(wait_pattern, "%s: the wait's result bits", name);
    print_user_data(name, wait_user_data);
    PRINTF(pattern_of(opened), "%s: the pattern after the delete", name);

    SceUInt32 timeout = TEST_WAIT_US;
    SceSize sent = NOT_SET;
    PRINTF(sceKernelSendMsgPipe(opened, "wxyz", 4, MSG_PIPE_MODE_FULL, &sent, &timeout), "%s: send through the opened UID", name);
    PRINTF(sent, "%s: the send's size", name);
    char got[8];
    sceClibMemset(got, 0, sizeof(got));
    SceSize size = NOT_SET;
    timeout = TEST_WAIT_US;
    PRINTF(sceKernelReceiveMsgPipe(opened, got, 4, MSG_PIPE_MODE_FULL, &size, &timeout), "%s: receive through the opened UID", name);
    PRINTF(size, "%s: the receive's size", name);
    PRINTF(sceClibMemcmp(got, "wxyz", 4) == 0, "%s: the bytes are wxyz", name);
    PRINTF(sceKernelCloseMsgPipe(opened), "%s: close the opened UID", name);
}

// A pipe with the auto reset attribute, where a poll takes the open bit and then the close bit
static void run_auto(const char *name) {
    pipe = make_pipe(EVENT_ATTR_AUTO_RESET);
    PRINTF(test_created(pipe), "%s: create the pipe", name);
    const SceUID opened = sceKernelOpenMsgPipe(pipe_name);
    PRINTF(test_created(opened), "%s: open it", name);
    PRINTF(pattern_of(pipe), "%s: the pattern after the open", name);
    print_poll(name, pipe, OPEN_BIT, 1);
    PRINTF(pattern_of(pipe), "%s: the pattern after the poll for the open bit", name);
    print_poll(name, pipe, OPEN_BIT, 0);
    PRINTF(sceKernelCloseMsgPipe(opened), "%s: close the opened UID", name);
    PRINTF(pattern_of(pipe), "%s: the pattern after the close", name);
    print_poll(name, pipe, CLOSE_BIT, 1);
    PRINTF(pattern_of(pipe), "%s: the pattern after the poll for the close bit", name);
    sceKernelDeleteMsgPipe(pipe);
}

int main(void) {
    main_thread = sceKernelGetThreadId();
    process = sceKernelGetProcessId();

    run_open_close("open then close");
    run_wait("wait for the open", OPEN_BIT);
    run_wait("wait for the close", CLOSE_BIT);
    run_twice("open and close twice");
    run_delete("delete the creating UID");
    run_auto("auto reset open and close");

    test_exit();
    return 0;
}
