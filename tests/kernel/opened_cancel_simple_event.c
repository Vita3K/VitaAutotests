// Cancels a simple event through an opened UID while two waiters queue through the creating UID,
// then cancels it again with a set pattern through the opened UID, and reads it back through the
// creating UID. It prints the count of the first cancel, each waiter's result and whether its user
// data is 0, the count of the second cancel, and whether the pattern and user data read afterwards
// are the ones the second cancel set. The model simple_event_cancel covers both cancels through
// the creating UID, and the cancels through an opened UID here are new. The object is made OPENABLE
// and opened by its name. Both waiters wait for the bit 0x01000000, and each wait has a timeout of 2
// seconds. A waiter is joined before its result is read.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NAME "Cancel event"
#define NOT_DONE 0xDEAD
#define USER_DATA 0x77

static SceUID wait_uid;
static volatile int results[2];
static SceUInt32 patterns[2];
static SceUInt64 user_datas[2];

// Waits for EVENT_BIT_A through wait_uid into slot i
static int wait_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    results[i] = sceKernelWaitEvent(wait_uid, EVENT_BIT_A, &patterns[i], &user_datas[i], &timeout);
    return 0;
}

int main(void) {
    const SceUID creating =
        sceKernelCreateSimpleEvent(NAME, SCE_KERNEL_ATTR_OPENABLE | EVENT_ATTR_MANUAL_RESET, 0, NULL);
    const SceUID opened = sceKernelOpenSimpleEvent(NAME);
    PRINTF(creating < 0 ? creating : opened < 0 ? opened : 0, "create and open the event");

    wait_uid = creating;
    SceUID threads[2];
    for (int i = 0; i < 2; i++) {
        results[i] = NOT_DONE;
        patterns[i] = NOT_DONE;
        user_datas[i] = NOT_DONE;
        threads[i] = test_start_thread_arg(wait_thread, i);
        test_wait_until_waiting(threads[i]);
    }
    SceUInt32 waiting = NOT_DONE;
    PRINT("cancel through the opened UID with 2 waiters", sceKernelCancelEvent(opened, &waiting));
    PRINT("the waiters the cancel counted", waiting);
    for (int i = 0; i < 2; i++) {
        test_join_thread(threads[i]);
        PRINTF(results[i], "waiter %d's result after the cancel", i);
        PRINTF(user_datas[i] == 0, "waiter %d's user data is 0 after the cancel", i);
    }

    waiting = NOT_DONE;
    PRINT("cancel and set EVENT_BIT_A with user data 0x77 through the opened UID",
          sceKernelCancelEventWithSetPattern(opened, EVENT_BIT_A, USER_DATA, &waiting));
    PRINT("the waiters the second cancel counted", waiting);
    EventInfo info = {.size = sizeof(info)};
    sceKernelGetEventInfo(creating, &info);
    PRINT("the pattern after, through the creating UID", info.eventPattern);
    PRINT("the user data after, through the creating UID", (SceUInt32)info.userData);
    PRINT("the pattern after is EVENT_BIT_A", info.eventPattern == EVENT_BIT_A);
    PRINT("the user data after is 0x77", info.userData == USER_DATA);

    sceKernelCloseSimpleEvent(opened);
    sceKernelDeleteSimpleEvent(creating);
    test_exit();
    return 0;
}
