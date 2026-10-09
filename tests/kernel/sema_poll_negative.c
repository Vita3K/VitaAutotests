// Polling a semaphore for a negative count, in its own test since a wrong implementation may crash or hang on it
#include <psp2/kernel/threadmgr.h>

#include "test.h"

int main(void) {
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 4, NULL);
    PRINT("poll for -1", sceKernelPollSema(sema, -1));

    test_exit();
    return 0;
}
