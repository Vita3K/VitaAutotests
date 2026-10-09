// Object names and opening objects by name
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// 32 characters, one past the longest name
#define LONG_NAME "abcdefghijklmnopqrstuvwxyz012345"

int main(void) {
    const SceUID sema = sceKernelCreateSema("open_me", ATTR_OPENABLE, 0, 1, NULL);
    const SceUID opened = sceKernelOpenSema("open_me");
    PRINT("open an openable semaphore", test_created(opened));
    sceKernelSignalSema(opened, 1);
    PRINT("the opened UID reaches the same semaphore", sceKernelPollSema(sema, 1));
    PRINT("close it", sceKernelCloseSema(opened));
    PRINT("the semaphore outlives the close", sceKernelSignalSema(sema, 1));

    sceKernelCreateSema("closed", 0, 0, 1, NULL);
    PRINT("open a semaphore that isn't openable", sceKernelOpenSema("closed") < 0);
    PRINT("open a name nothing has", sceKernelOpenSema("nothing") < 0);

    // Whether a name past 31 characters is an error for objects that are openable or not
    PRINT("a long name on an openable semaphore", test_created(sceKernelCreateSema(LONG_NAME, ATTR_OPENABLE, 0, 1, NULL)));
    PRINT("a long name on a semaphore that isn't openable", test_created(sceKernelCreateSema(LONG_NAME, 0, 0, 1, NULL)));

    test_exit();
    return 0;
}
