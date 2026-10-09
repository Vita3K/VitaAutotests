#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID flag;
static int wait_result[2];
static unsigned int wait_bits[2];

static int wait_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    wait_result[i] = sceKernelWaitEventFlag(flag, 0x8, EVF_WAITMODE_AND, &wait_bits[i], NULL);
    return 0;
}

// Starts a thread that waits for bit 0x8 into slot i of the results
static SceUID start_waiter(int i) {
    wait_result[i] = -1;
    wait_bits[i] = 0xDEAD;
    const SceUID thread = test_start_thread_arg(wait_thread, i);
    test_wait_until_waiting(thread);
    return thread;
}

int main(void) {
    unsigned int bits = 0;
    flag = sceKernelCreateEventFlag("flag", 0, 0x3, NULL);

    PRINT("poll AND with a bit missing", sceKernelPollEventFlag(flag, 0x7, EVF_WAITMODE_AND, &bits));
    PRINT("poll OR with one bit set", sceKernelPollEventFlag(flag, 0x5, EVF_WAITMODE_OR, &bits));
    PRINT("poll gives the bits", bits);
    PRINT("poll that clears its pattern", sceKernelPollEventFlag(flag, 0x1, EVF_WAITMODE_AND | EVF_WAITMODE_CLEAR_PAT, &bits));
    PRINT("poll gives the bits from before clearing", bits);
    sceKernelPollEventFlag(flag, 0x2, EVF_WAITMODE_OR, &bits);
    PRINT("clearing the pattern clears only it", bits);
    sceKernelPollEventFlag(flag, 0x2, EVF_WAITMODE_OR | EVF_WAITMODE_CLEAR_ALL, &bits);
    PRINT("clearing all clears every bit", sceKernelPollEventFlag(flag, 0xFF, EVF_WAITMODE_OR, &bits));

    sceKernelSetEventFlag(flag, 0x4);
    SceUInt timeout = 1000;
    PRINT("wait times out", sceKernelWaitEventFlag(flag, 0x1, EVF_WAITMODE_AND, &bits, &timeout));
    PRINT("a timed out wait gives the bits", bits);
    sceKernelPollEventFlag(flag, 0x4, EVF_WAITMODE_OR | EVF_WAITMODE_CLEAR_ALL, &bits);

    // Without the multi attribute, only one thread may wait
    SceUID waiter = start_waiter(0);
    timeout = 1000;
    PRINT("a second waiter without the multi attribute", sceKernelWaitEventFlag(flag, 0x1, EVF_WAITMODE_AND, &bits, &timeout));
    SceUInt32 waiting = 0;
    PRINT("cancel", sceKernelCancelEventFlag(flag, 0x5, &waiting));
    test_join_thread(waiter);
    PRINT("cancel counts the waiting threads", waiting);
    PRINT("a canceled wait", wait_result[0]);
    PRINT("a canceled wait gives the cancel pattern", wait_bits[0]);

    waiter = start_waiter(0);
    PRINT("delete", sceKernelDeleteEventFlag(flag));
    test_join_thread(waiter);
    PRINT("a wait on a deleted flag", wait_result[0]);

    // With the multi attribute, setting the bit ends every wait
    flag = sceKernelCreateEventFlag("multi", EVF_ATTR_MULTI, 0, NULL);
    const SceUID first = start_waiter(0);
    const SceUID second = start_waiter(1);
    sceKernelSetEventFlag(flag, 0x8);
    test_join_thread(first);
    test_join_thread(second);
    PRINT("the first of two waiters", wait_result[0]);
    PRINT("the second of two waiters", wait_result[1]);

    test_exit();
    return 0;
}
