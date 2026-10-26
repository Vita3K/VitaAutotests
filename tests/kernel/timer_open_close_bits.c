// The bits a timer gets when it is opened and closed, and what the waits and polls for them get.
// The timer is openable, and the main thread's first waiter waits on the creating UID for the open
// bit 0x100 with a timeout of 2 s. The main thread opens the timer by name, which ends that wait. A
// second waiter then waits for the close bit 0x200, and closing the opened UID ends it. Last, the
// test polls for 0x200 and 0x100. The test prints each call's result, the bits each wait or poll
// got, whether the high word of its user data is the main thread's UID and its low word the process
// ID, and the timer's pattern after each step.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NAME "open close bits"
#define OPEN_BIT 0x100
#define CLOSE_BIT 0x200
#define NOT_DONE 0xDEAD

static SceUID main_thread, process, timer;
static int wait_result;
static SceUInt32 wait_bits;
static SceUInt64 wait_user_data;

// Waits on the timer for the bit in argp, and keeps what the wait returned
static int waiter(SceSize args, void *argp) {
    (void)args;
    const int bit = *(const int *)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    SceUInt32 bits = NOT_DONE;
    SceUInt64 user_data = 0;
    wait_result = sceKernelWaitEvent(timer, (SceUInt32)bit, &bits, &user_data, &timeout);
    wait_bits = bits;
    wait_user_data = user_data;
    return 0;
}

static SceUInt32 pattern_of(SceUID id) {
    EventInfo info = { .size = sizeof(info) };
    sceKernelGetEventInfo(id, &info);
    return info.eventPattern;
}

static void print_user_data(const char *name, SceUInt64 user_data) {
    PRINTF((SceUInt32)(user_data >> 32) == (SceUInt32)main_thread,
           "%s: the high word of the user data is the main thread's UID", name);
    PRINTF((SceUInt32)user_data == (SceUInt32)process,
           "%s: the low word of the user data is the process ID", name);
}

// Polls the timer for bit, printing the poll, the bits it got, the user data and the pattern after
static void print_poll(const char *name, SceUInt32 bit) {
    SceUInt32 bits = NOT_DONE;
    SceUInt64 user_data = 0;
    const int polled = sceKernelPollEvent(timer, bit, &bits, &user_data);
    PRINTF(polled, "%s: poll for 0x%X", name, bit);
    PRINTF(bits, "%s: poll for 0x%X, the bits it got", name, bit);
    print_user_data(name, user_data);
    PRINTF(pattern_of(timer), "%s: the pattern after the poll for 0x%X", name, bit);
}

int main(void) {
    main_thread = sceKernelGetThreadId();
    process = sceKernelGetProcessId();

    timer = sceKernelCreateTimer(NAME, SCE_KERNEL_ATTR_OPENABLE, NULL);
    PRINTF(test_created(timer), "create the timer");
    PRINT("the pattern after the create", pattern_of(timer));

    // A waiter for the open bit, ended by opening the timer by name
    int bit = OPEN_BIT;
    SceUID thread = test_start_thread_arg(waiter, bit);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_EVENT, timer));
    const SceUID opened = sceKernelOpenTimer(NAME);
    PRINTF(test_created(opened), "open the timer by name");
    test_join_thread(thread);
    PRINT("the wait for the open bit", wait_result);
    PRINT("the wait for the open bit, the bits it got", wait_bits);
    print_user_data("the wait for the open bit", wait_user_data);
    PRINT("the pattern after the open", pattern_of(timer));

    // A waiter for the close bit, ended by closing the opened UID
    bit = CLOSE_BIT;
    thread = test_start_thread_arg(waiter, bit);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_EVENT, timer) ||
                  test_in_wait(thread, WAIT_TYPE_EVENT, opened));
    PRINT("close the opened UID", sceKernelCloseTimer(opened));
    test_join_thread(thread);
    PRINT("the wait for the close bit", wait_result);
    PRINT("the wait for the close bit, the bits it got", wait_bits);
    print_user_data("the wait for the close bit", wait_user_data);
    PRINT("the pattern after the close", pattern_of(timer));

    print_poll("the poll for the close bit", CLOSE_BIT);
    print_poll("the poll for the open bit", OPEN_BIT);

    sceKernelDeleteTimer(timer);
    test_exit();
    return 0;
}
