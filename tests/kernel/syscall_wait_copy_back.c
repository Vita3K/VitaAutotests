// What the wait and lightweight mutex lock syscalls write back when the wait fails, and the order
// of the lock's checks: an unmapped timeout pointer against a NULL work area.
//
// On a PS Vita TV on firmware 3.65 a wait on a bad id writes 0 to the result pattern and the user
// data, as the call waits on copies set to 0 and writes them back after any wait. The lock copies
// the timeout in before it looks at the work area, so an unmapped timeout gives 0x80022005 there.
#include <psp2/kernel/threadmgr.h>

#include "syscalls.h"
#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    SceUInt32 pattern = 0xDEAD;
    const WaitEventFlagBlock flag_block = { &pattern, NULL };
    PRINT("wait on a bad flag id", syscall_wait_event_flag(-1, 0x1, SCE_EVENT_WAITOR, &flag_block));
    PRINT("the result pattern after it", pattern);

    pattern = 0xDEAD;
    SceUInt64 user_data = 0xDEAD0000BEEFULL << 16;
    const WaitEventBlock event_block = { &user_data, NULL };
    PRINT("wait on a bad event id", syscall_wait_event(-1, EVENT_BIT_A, &pattern, &event_block));
    PRINT("the result pattern after it", pattern);
    PRINT("the user data's low word after it", (SceUInt32)user_data);
    PRINT("the user data's high word after it", (SceUInt32)(user_data >> 32));

    PRINT("lock no work area with an unmapped timeout", syscall_lock_lw_mutex(NULL, 1, (SceUInt32 *)0x10, 0));

    SceKernelLwMutexWork work;
    PRINT("create a lightweight mutex", sceKernelCreateLwMutex(&work, "lw", 0, 0, NULL));
    PRINT("lock it with an unmapped timeout", syscall_lock_lw_mutex(&work, 1, (SceUInt32 *)0x10, 0));
    SceUInt32 timeout = 1000;
    PRINT("lock it with a timeout", syscall_lock_lw_mutex(&work, 1, &timeout, 0));
    PRINT("the timeout after it", timeout);
    sceKernelUnlockLwMutex(&work, 1);
    sceKernelDeleteLwMutex(&work);

    test_exit();
    return 0;
}
