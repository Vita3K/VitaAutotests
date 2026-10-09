// A thread waiting for its own end, in its own test since a wrong implementation may crash or hang on it
#include <psp2/kernel/threadmgr.h>

#include "test.h"

int main(void) {
    int status = 0;
    SceUInt timeout = 1000;
    PRINT("wait for the own end", sceKernelWaitThreadEnd(sceKernelGetThreadId(), &status, &timeout));

    test_exit();
    return 0;
}
