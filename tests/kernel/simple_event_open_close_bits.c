// The bits a simple event gets when it is opened and closed. Every case makes a fresh event with the
// openable attribute and pattern 0, then sets bit A with a marker as the user data, so a user data
// that the open or close writes can be told from the one the event had before. The bits are the
// open bit 0x100 and the close bit 0x200. The user data a poll or wait gets prints as three
// comparisons: whether its high word is the main thread's UID, whether its low word is the
// process ID, and whether it is still the marker. The cases:
// - an event opened by its name and then closed through the opened UID. After the open the test
//   reads the pattern, polls for the close bit, which isn't set yet, and polls for the open bit.
//   After the close it reads the pattern and polls for the close bit.
// - a thread waiting for the open bit through the creating UID, ended or not by the main thread
//   opening the event, with the wait's result, the bits and user data it got and the pattern after
// - the same for a thread waiting for the close bit, with the event opened before and the opened
//   UID closed
// - the same two waits on an event with the auto reset attribute, where the bit the woken thread
//   takes may be cleared
// - an event opened twice and closed twice through the two opened UIDs, with the pattern after
//   each call
// - the same on an auto reset event, where the test polls for the bit after each call, which
//   takes the bit and shows whether that call set it
// Every wait has a timeout of 6 seconds, far past what the main thread needs to end it, and the
// ones the open or close doesn't end print the timeout's result.
//
// On a PS Vita TV on firmware 3.65 an open sets the bit 0x100 and a close sets 0x200, each time,
// with user data that holds the opening or closing thread's UID in its high word and the process ID
// in its low word. A waiter for either bit is woken with that user data. On a manual reset event
// the bits stay, and on an auto reset event the woken waiter or the poll takes them.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define OPEN_BIT 0x100
#define CLOSE_BIT 0x200
#define MARKER 0x1122334455667788ull
#define WAIT_TIMEOUT_US (3 * TEST_WAIT_US)

static SceUID main_thread, process;
static char event_name[32];
static SceUID event;
static SceUInt32 wait_pattern;
static SceUInt64 wait_user_data;
static int wait_result;

// Creates a fresh event under a name of its own, with bit A set by a set with the marker
static SceUID make_event(SceUInt32 attr) {
    static int made;
    sceClibSnprintf(event_name, sizeof(event_name), "OpenBits %d", ++made);
    const SceUID id =
        sceKernelCreateSimpleEvent(event_name, SCE_KERNEL_ATTR_OPENABLE | attr, 0, NULL);
    if (id >= 0)
        sceKernelSetEvent(id, EVENT_BIT_A, MARKER);
    return id;
}

static SceUInt32 pattern_of(SceUID id) {
    SceUInt32 pattern = 0xDEAD;
    sceKernelGetEventPattern(id, &pattern);
    return pattern;
}

static void print_user_data(const char *name, SceUInt64 user_data) {
    PRINTF((SceUInt32)(user_data >> 32) == (SceUInt32)main_thread,
        "%s: the high word of the user data is the main thread's UID", name);
    PRINTF((SceUInt32)user_data == (SceUInt32)process,
        "%s: the low word of the user data is the process ID", name);
    PRINTF(user_data == MARKER, "%s: the user data is still the marker", name);
}

// Polls id for bit, printing the poll and what it got, with the user data if detail is set
static void print_poll(const char *name, SceUID id, SceUInt32 bit, int detail) {
    SceUInt32 result = 0;
    SceUInt64 user_data = 0;
    const int polled = sceKernelPollEvent(id, bit, &result, &user_data);
    PRINTF(polled, "%s: poll for 0x%X", name, bit);
    if (polled < 0 || !detail)
        return;
    PRINTF(result, "%s: poll for 0x%X, the bits it got", name, bit);
    print_user_data(name, user_data);
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    const SceUInt32 bits = *(const SceUInt32 *)argp;
    SceUInt32 timeout = WAIT_TIMEOUT_US;
    SceUInt32 result = 0;
    SceUInt64 user_data = 0;
    wait_result = sceKernelWaitEvent(event, bits, &result, &user_data, &timeout);
    wait_pattern = result;
    wait_user_data = user_data;
    return 0;
}

// An event opened and then closed, with a poll for each bit
static void run_open_close(void) {
    const char *name = "open then close";
    const SceUID id = make_event(EVENT_ATTR_MANUAL_RESET);
    PRINTF(test_created(id), "%s: create the event", name);
    PRINTF(pattern_of(id), "%s: the pattern before the open", name);
    const SceUID opened = sceKernelOpenSimpleEvent(event_name);
    PRINTF(test_created(opened), "%s: open it", name);
    PRINTF(pattern_of(id), "%s: the pattern after the open", name);
    print_poll(name, id, CLOSE_BIT, 1);
    print_poll(name, id, OPEN_BIT, 1);
    PRINTF(pattern_of(id), "%s: the pattern after the polls", name);
    PRINTF(sceKernelCloseSimpleEvent(opened), "%s: close the opened UID", name);
    PRINTF(pattern_of(id), "%s: the pattern after the close", name);
    print_poll(name, id, CLOSE_BIT, 1);
    PRINTF(pattern_of(id), "%s: the pattern after the poll", name);
    sceKernelDeleteSimpleEvent(id);
}

// A thread waiting for bit, which the open or the close of the event is meant to end
static void run_wait(const char *name, SceUInt32 attr, SceUInt32 bit) {
    event = make_event(attr);
    PRINTF(test_created(event), "%s: create the event", name);
    SceUID opened = -1;
    if (bit == CLOSE_BIT) {
        opened = sceKernelOpenSimpleEvent(event_name);
        PRINTF(test_created(opened), "%s: open it first", name);
    }
    wait_result = 0xDEAD;
    wait_pattern = 0xDEAD;
    wait_user_data = 0;
    const SceUID thread = test_start_thread_arg(waiter, bit);
    // With the event opened, the thread's info may report either of its UIDs as the one it waits on
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_EVENT, event) || (opened >= 0 && test_in_wait(thread, WAIT_TYPE_EVENT, opened)));
    if (bit == OPEN_BIT) {
        opened = sceKernelOpenSimpleEvent(event_name);
        PRINTF(test_created(opened), "%s: open it", name);
    } else {
        PRINTF(sceKernelCloseSimpleEvent(opened), "%s: close the opened UID", name);
    }
    test_join_thread(thread);
    PRINTF(wait_result, "%s: the wait", name);
    PRINTF(wait_pattern, "%s: the wait's result pattern", name);
    print_user_data(name, wait_user_data);
    PRINTF(pattern_of(event), "%s: the pattern after", name);
    if (bit == OPEN_BIT)
        sceKernelCloseSimpleEvent(opened);
    sceKernelDeleteSimpleEvent(event);
}

// Opens twice and closes twice. With take set, polls for the call's bit after each call, which
// clears it on an auto reset event.
static void run_twice(const char *name, SceUInt32 attr, int take) {
    const SceUID id = make_event(attr);
    PRINTF(test_created(id), "%s: create the event", name);
    SceUID opened[2];
    for (int i = 0; i < 2; i++) {
        opened[i] = sceKernelOpenSimpleEvent(event_name);
        PRINTF(test_created(opened[i]), "%s: open number %d", name, i + 1);
        PRINTF(pattern_of(id), "%s: the pattern after open number %d", name, i + 1);
        if (take) {
            print_poll(name, id, OPEN_BIT, 0);
            PRINTF(pattern_of(id), "%s: the pattern after the poll after open number %d", name,
                i + 1);
        }
    }
    for (int i = 0; i < 2; i++) {
        PRINTF(sceKernelCloseSimpleEvent(opened[i]), "%s: close number %d", name, i + 1);
        PRINTF(pattern_of(id), "%s: the pattern after close number %d", name, i + 1);
        if (take) {
            print_poll(name, id, CLOSE_BIT, 0);
            PRINTF(pattern_of(id), "%s: the pattern after the poll after close number %d", name,
                i + 1);
        }
    }
    sceKernelDeleteSimpleEvent(id);
}

int main(void) {
    main_thread = sceKernelGetThreadId();
    process = sceKernelGetProcessId();

    run_open_close();
    run_wait("wait for the open", EVENT_ATTR_MANUAL_RESET, OPEN_BIT);
    run_wait("wait for the close", EVENT_ATTR_MANUAL_RESET, CLOSE_BIT);
    run_wait("auto reset wait for the open", EVENT_ATTR_AUTO_RESET, OPEN_BIT);
    run_wait("auto reset wait for the close", EVENT_ATTR_AUTO_RESET, CLOSE_BIT);
    run_twice("open and close twice", EVENT_ATTR_MANUAL_RESET, 0);
    run_twice("auto reset open and close twice", EVENT_ATTR_AUTO_RESET, 1);

    test_exit();
    return 0;
}
