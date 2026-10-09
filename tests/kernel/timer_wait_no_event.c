// Waiting on a timer with no event set, in its own test since a wrong implementation may crash or hang on it
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    const SceUID timer = sceKernelCreateTimer("timer", 0, NULL);
    SceUInt32 result;
    SceUInt64 user_data;
    SceUInt32 timeout = 1000;
    PRINT("wait on a timer with no event set", sceKernelWaitEvent(timer, TIMER_EVENT_BIT, &result, &user_data, &timeout));

    test_exit();
    return 0;
}
