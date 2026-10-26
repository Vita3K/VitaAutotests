// Polls of a WAITMULTIPLE event flag while a waiter for another bit is queued on it. The flag
// starts with bit 0x2 set. A waits for bit 0x1 with AND and a 2 s timeout, so it is queued. The
// main thread polls 0x2 with WAITOR and WAITCLEAR, which takes that bit, then polls 0x1, which
// isn't set, then sets 0x1 and joins A. The model event_flag_waits covers several waiters on one
// flag. The cases:
// - the poll for 0x2: its result and the bits it got.
// - the poll for 0x1: its result and the bits it got, 0xDEAD if it didn't set them. A failed poll
//   writes the current bits of the flag.
// - whether A ended before the set.
// - A's result and the bits it got.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID flag;
static volatile int waiter_ended, waiter_result;
static volatile SceUInt32 waiter_bits;

static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 bits = 0xDEAD;
    SceUInt32 timeout = TEST_WAIT_US;
    waiter_result = sceKernelWaitEventFlag(flag, 0x1, SCE_EVENT_WAITAND, &bits, &timeout);
    waiter_bits = bits;
    waiter_ended = 1;
    return 0;
}

int main(void) {
    flag = sceKernelCreateEventFlag("flag", SCE_EVENT_WAITMULTIPLE, 0x2, NULL);
    waiter_ended = 0;
    waiter_result = 0xDEAD;
    waiter_bits = 0xDEAD;

    const SceUID a = test_start_thread(waiter_entry);
    TEST_WAIT_FOR(test_in_wait(a, WAIT_TYPE_EVENT_FLAG, flag));

    SceUInt32 bits = 0xDEAD;
    PRINT("poll 0x2 with WAITOR and WAITCLEAR",
          sceKernelPollEventFlag(flag, 0x2, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR, &bits));
    PRINT("the bits the poll for 0x2 got", bits);
    bits = 0xDEAD;
    PRINT("poll 0x1 with WAITOR", sceKernelPollEventFlag(flag, 0x1, SCE_EVENT_WAITOR, &bits));
    PRINT("the bits the poll for 0x1 got", bits);
    PRINT("A ended before the set", waiter_ended);

    sceKernelSetEventFlag(flag, 0x1);
    test_join_thread(a);
    PRINT("A's result", waiter_result);
    PRINT("the bits A got", waiter_bits);

    sceKernelDeleteEventFlag(flag);
    test_exit();
    return 0;
}
