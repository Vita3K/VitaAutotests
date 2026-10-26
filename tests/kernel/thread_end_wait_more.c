// Waiting for a thread's end in the cases thread_end_waits leaves out:
// - three threads waiting for one thread's end all get its exit status
// - a waiter whose timeout passes before the end, which prints its result, the status it left alone
//   and the time left it wrote
// - a waiter for a thread that already ended, which returns at once with the status
// thread_end_waits already covers a thread that exits and deletes itself, a waiter with callbacks
// that is notified during the wait and waits again (so that case isn't repeated here), two waiters
// where one times out and the other gets the end, and a waiter on the end of a thread that is in a
// callback. Its timeout case prints the result and the status only, so the time left is new here.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define EXIT_STATUS 0x77
#define WAITERS 3
#define SHORT_TIMEOUT_US 300000
#define LONG_TIMEOUT_US (2 * TEST_WAIT_US)

static SceUID go, target;
static SceUInt waiter_timeout[WAITERS];
static volatile int waiter_result[WAITERS], waiter_status[WAITERS];
static volatile SceUInt waiter_left[WAITERS];

// Returns EXIT_STATUS once go is signaled
static int target_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = LONG_TIMEOUT_US;
    sceKernelWaitSema(go, 1, &timeout);
    return EXIT_STATUS;
}

// Waits for the target's end with the timeout of its slot, and records the
// result, the status it wrote and the time left
static int end_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    int status = 0xDEAD;
    SceUInt timeout = waiter_timeout[i];
    waiter_result[i] = sceKernelWaitThreadEnd(target, &status, &timeout);
    waiter_status[i] = status;
    waiter_left[i] = timeout;
    return 0;
}

static SceUID start_waiter(int i) {
    waiter_result[i] = waiter_status[i] = 0xDEAD;
    waiter_left[i] = 0xDEAD;
    const SceUID thread = test_start_thread_arg(end_waiter, i);
    test_wait_until_waiting(thread);
    return thread;
}

int main(void) {
    // Three waiters on one target, which all get the status it returns
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    target = test_start_thread(target_thread);
    test_wait_until_waiting(target);
    SceUID waiters[WAITERS];
    for (int i = 0; i < WAITERS; i++) {
        waiter_timeout[i] = LONG_TIMEOUT_US;
        waiters[i] = start_waiter(i);
    }
    sceKernelSignalSema(go, 1);
    for (int i = 0; i < WAITERS; i++)
        test_join_thread(waiters[i]);
    for (int i = 0; i < WAITERS; i++) {
        PRINTF(waiter_result[i], "three waiters: waiter %d, the wait for its end",
            i);
        PRINTF(waiter_status[i], "three waiters: waiter %d, its status", i);
    }

    // A dormant thread, which already ended but isn't deleted, answers a new
    // waiter at once
    int status = 0xDEAD;
    SceUInt timeout = LONG_TIMEOUT_US;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    PRINT("ended thread: the wait for its end",
        sceKernelWaitThreadEnd(target, &status, &timeout));
    PRINT("ended thread: it returned at once",
        sceKernelGetProcessTimeWide() - start < 1000000);
    PRINT("ended thread: the status", status);
    sceKernelDeleteThread(target);

    // A waiter whose timeout passes before the target ends. The target only ends
    // after the waiter is done, so the timeout is the only way this wait can end.
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    target = test_start_thread(target_thread);
    test_wait_until_waiting(target);
    waiter_timeout[0] = SHORT_TIMEOUT_US;
    const SceUID timed_out = start_waiter(0);
    test_join_thread(timed_out);
    PRINT("timeout: the wait for its end", waiter_result[0]);
    PRINT("timeout: the status it left alone", waiter_status[0] == 0xDEAD);
    PRINT("timeout: the time left", waiter_left[0]);
    sceKernelSignalSema(go, 1);
    sceKernelWaitThreadEnd(target, NULL, NULL);
    sceKernelDeleteThread(target);

    test_exit();
    return 0;
}
