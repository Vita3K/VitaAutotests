// Creating a thread with a negative stack size, in its own test since a wrong implementation may crash or hang on it
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

int main(void) {
    PRINT("create a thread with a negative stack", test_created(sceKernelCreateThread("t", entry, 0x10000100, -1, 0, 0, NULL)));

    test_exit();
    return 0;
}
