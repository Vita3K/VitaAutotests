// Setting a timer event with an interval of 0, in its own test since the call may never return
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    const SceUID timer = sceKernelCreateTimer("timer", 0, NULL);
    SceUInt64 interval = 0;
    PRINT("set an event with an interval of 0", sceKernelSetTimerEvent(timer, 1, &interval, 0));

    test_exit();
    return 0;
}
