// What the waiters of a timer get when the timer is deleted while they wait for pattern 0. Thread A
// waits on the timer for pattern 0 with a timeout of 8 s, and thread B waits the same way with
// callbacks, through sceKernelWaitEventCB. Each one starts and is seen queued on the timer before
// the next starts. Then the main thread deletes the timer. The test prints the result of the delete
// and, for each wait, its result, the bits it got, and whether the high word of its user data is
// the deleting thread's UID and its low word is the process ID. The out values start as 0xDEAD.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD

static SceUID timer;
static int results[2];
static SceUInt32 bits[2];
static SceUInt64 user_data[2];

// Waits for pattern 0 on the timer into slot i, with callbacks when i is 1
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt32 timeout = 4 * TEST_WAIT_US;
    SceUInt32 got = NOT_DONE;
    SceUInt64 data = NOT_DONE;
    if (i == 0)
        results[i] = sceKernelWaitEvent(timer, 0, &got, &data, &timeout);
    else
        results[i] = sceKernelWaitEventCB(timer, 0, &got, &data, &timeout);
    bits[i] = got;
    user_data[i] = data;
    return 0;
}

int main(void) {
    const SceUID self = sceKernelGetThreadId();
    const SceUID process = sceKernelGetProcessId();
    timer = sceKernelCreateTimer("zero", 0, NULL);

    SceUID threads[2];
    for (int i = 0; i < 2; i++) {
        results[i] = NOT_DONE;
        threads[i] = test_start_thread_arg(waiter_entry, i);
        const SceUInt32 type = i == 0 ? WAIT_TYPE_EVENT : WAIT_TYPE_EVENT | WAIT_TYPE_CB;
        TEST_WAIT_FOR(test_in_wait(threads[i], type, timer));
    }
    PRINT("the delete", sceKernelDeleteTimer(timer));
    for (int i = 0; i < 2; i++)
        test_join_thread(threads[i]);

    static const char *const names[] = { "the wait for pattern 0",
                                         "the wait for pattern 0 with callbacks" };
    for (int i = 0; i < 2; i++) {
        PRINTF(results[i], "%s", names[i]);
        PRINTF(bits[i], "%s: the bits it got", names[i]);
        PRINTF((SceUInt32)(user_data[i] >> 32) == (SceUInt32)self,
               "%s: the high word of its user data is the deleting thread's UID", names[i]);
        PRINTF((SceUInt32)user_data[i] == (SceUInt32)process,
               "%s: the low word of its user data is the process ID", names[i]);
    }
    test_exit();
    return 0;
}
