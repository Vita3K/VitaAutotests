// What the thread that runs main reports about itself before the test changes anything: its name,
// attributes, priorities, stack size, CPU affinity masks, and whether its current core is one of
// the user cores 0 to 2. The test reads them first thing, without a delay. It also prints whether
// the name ends with " EntryCall", the name of the threads that run module start functions.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    const int result = sceKernelGetThreadInfo(0, &info);
    const int cpu = sceKernelGetCpuId();
    const int name_len = (int)sceClibStrnlen(info.name, sizeof(info.name));
    const int entry_call = name_len >= 10 && sceClibStrcmp(info.name + name_len - 10, " EntryCall") == 0;

    PRINT("the info call", result);
    PRINTF(1, "the name is %s", info.name);
    PRINT("the name ends with EntryCall", entry_call);
    PRINT("the attributes", info.attr);
    PRINT("the initial priority", info.initPriority);
    PRINT("the current priority", info.currentPriority);
    PRINT("the stack size", info.stackSize);
    PRINT("the initial affinity mask", info.initCpuAffinityMask);
    PRINT("the current affinity mask", info.currentCpuAffinityMask);
    PRINT("the current CPU is a user core", cpu >= 0 && cpu <= 2);
    PRINT("the current CPU field is a user core", info.currentCpuId >= 0 && info.currentCpuId <= 2);
    test_exit();
    return 0;
}
