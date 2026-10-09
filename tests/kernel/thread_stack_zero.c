// Creating a thread with a stack size of 0, in its own test since a wrong implementation may crash or hang on it
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

int main(void) {
    PRINT("create a thread with a stack of 0", test_created(sceKernelCreateThread("t", entry, 0x10000100, 0, 0, 0, NULL)));

    test_exit();
    return 0;
}
