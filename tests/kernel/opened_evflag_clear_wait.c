// Event flag waits that clear bits, through an opened UID and the creating UID, and a wait mode
// that the call refuses. The flag is made OPENABLE and opened by its name. The cases, in order:
// - the bits 0x3 set through the creating UID, then an OR wait for 0x1 with WAITCLEAR through the
//   opened UID, which should return at once with the bits it saw, and a poll for 0x3 through the
//   creating UID after it
// - a WAITCLEAR_PAT waiter for the bit 0x2 queued through the creating UID, woken by the set of 0x6
//   through the opened UID, so only the bit it waited for should be cleared
// - a wait with WAITCLEAR and WAITCLEAR_PAT together through the opened UID, which the call should
//   refuse
// Then the pattern is read through both UIDs and the two are compared. Every wait has a timeout of
// 2 seconds, and the waiter is joined before its out bits are read. The model opened_outlives_delete
// covers an event flag through its opened UIDs after the creating UID is deleted, and the wait
// modes here are new.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NAME "Clear wait"
#define NOT_DONE 0xDEAD

static SceUID wait_uid;
static volatile int wait_result;
static volatile unsigned int wait_bits = NOT_DONE;

// Waits for the bit 0x2 with WAITCLEAR_PAT, and keeps the result and the out bits
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    unsigned int bits = NOT_DONE;
    wait_result =
        sceKernelWaitEventFlag(wait_uid, 0x2, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, &bits, &timeout);
    wait_bits = bits;
    return 0;
}

// The pattern the info call through id reports, or its error
static int pattern(SceUID id) {
    SceKernelEventFlagInfo info = {.size = sizeof(info)};
    const int result = sceKernelGetEventFlagInfo(id, &info);
    return result < 0 ? result : (int)info.currentPattern;
}

int main(void) {
    const SceUID creating = sceKernelCreateEventFlag(NAME, SCE_KERNEL_ATTR_OPENABLE, 0, NULL);
    const SceUID opened = sceKernelOpenEventFlag(NAME);
    PRINTF(creating < 0 ? creating : opened < 0 ? opened : 0, "create and open the flag");
    PRINT("set 0x3 through the creating UID", sceKernelSetEventFlag(creating, 0x3));

    unsigned int bits = NOT_DONE;
    SceUInt timeout = TEST_WAIT_US;
    PRINT("OR wait for 0x1 with WAITCLEAR through the opened UID",
          sceKernelWaitEventFlag(opened, 0x1, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR, &bits, &timeout));
    PRINT("its out bits", bits);
    unsigned int polled = NOT_DONE;
    PRINT("poll for 0x3 OR through the creating UID after it",
          sceKernelPollEventFlag(creating, 0x3, SCE_EVENT_WAITOR, &polled));

    wait_uid = creating;
    wait_result = NOT_DONE;
    const SceUID thread = test_start_thread(waiter);
    test_wait_until_waiting(thread);
    PRINT("set 0x6 through the opened UID", sceKernelSetEventFlag(opened, 0x6));
    test_join_thread(thread);
    PRINT("the WAITCLEAR_PAT waiter's result", wait_result);
    PRINT("the WAITCLEAR_PAT waiter's out bits", wait_bits);

    bits = NOT_DONE;
    timeout = TEST_WAIT_US;
    PRINT("WAITCLEAR and WAITCLEAR_PAT together for 0x1 through the opened UID",
          sceKernelWaitEventFlag(opened, 0x1,
                                 SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR | SCE_EVENT_WAITCLEAR_PAT, &bits,
                                 &timeout));
    PRINT("its out bits", bits);

    const int through_creating = pattern(creating);
    PRINT("the pattern after, through the creating UID", through_creating);
    PRINT("the pattern through the opened UID is the same", pattern(opened) == through_creating);

    sceKernelCloseEventFlag(opened);
    sceKernelDeleteEventFlag(creating);
    test_exit();
    return 0;
}
