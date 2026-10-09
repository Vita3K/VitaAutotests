// Event flag waits with several waiters: which ones a set wakes, and clearing between them
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID flag;
static unsigned int wait_pattern[2], wait_mode[2];
static int wait_result[2];

// Waits for wait_pattern[i] with wait_mode[i] into slot i
static int wait_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    unsigned int bits;
    wait_result[i] = sceKernelWaitEventFlag(flag, wait_pattern[i], wait_mode[i], &bits, &timeout);
    return 0;
}

// Starts a waiter for each slot, the first one first
static void start_waiters(SceUID threads[2]) {
    for (int i = 0; i < 2; i++) {
        wait_result[i] = -1;
        threads[i] = test_start_thread_arg(wait_thread, i);
        test_wait_until_waiting(threads[i]);
    }
}

int main(void) {
    SceUID threads[2];
    unsigned int bits = 0;
    flag = sceKernelCreateEventFlag("flag", EVF_ATTR_MULTI, 0, NULL);

    // A set wakes only the waiters its bits satisfy
    wait_pattern[0] = 0x1;
    wait_pattern[1] = 0x2;
    wait_mode[0] = wait_mode[1] = EVF_WAITMODE_AND;
    start_waiters(threads);
    sceKernelSetEventFlag(flag, 0x2);
    test_join_thread(threads[0]);
    test_join_thread(threads[1]);
    PRINT("the waiter for another bit times out", wait_result[0]);
    PRINT("the waiter for the set bit wakes", wait_result[1]);
    sceKernelPollEventFlag(flag, 0xFF, EVF_WAITMODE_OR | EVF_WAITMODE_CLEAR_ALL, &bits);

    // A waiter that clears takes the bits before the waiters behind it see them
    wait_pattern[0] = wait_pattern[1] = 0x1;
    wait_mode[0] = EVF_WAITMODE_AND | EVF_WAITMODE_CLEAR_ALL;
    wait_mode[1] = EVF_WAITMODE_AND;
    start_waiters(threads);
    sceKernelSetEventFlag(flag, 0x1);
    test_join_thread(threads[0]);
    test_join_thread(threads[1]);
    PRINT("the first waiter, which clears, wakes", wait_result[0]);
    PRINT("the second one doesn't see the bit", wait_result[1]);
    PRINT("the bits are clear after it", sceKernelPollEventFlag(flag, 0xFF, EVF_WAITMODE_OR, &bits));

    // A clearing waiter behind one that doesn't clear: both wake
    wait_mode[0] = EVF_WAITMODE_AND;
    wait_mode[1] = EVF_WAITMODE_AND | EVF_WAITMODE_CLEAR_PAT;
    start_waiters(threads);
    sceKernelSetEventFlag(flag, 0x1);
    test_join_thread(threads[0]);
    test_join_thread(threads[1]);
    PRINT("the first waiter wakes", wait_result[0]);
    PRINT("the clearing waiter behind it wakes too", wait_result[1]);
    PRINT("and clears the bit", sceKernelPollEventFlag(flag, 0x1, EVF_WAITMODE_OR, &bits));

    test_exit();
    return 0;
}
