// Creating threads with bad arguments, in its own test since a wrong implementation may crash on them
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Returns 0 if a thread can be created like this, or the error
static int create(const char *name, SceKernelThreadEntry fn, int stack_size) {
    const SceUID thread = sceKernelCreateThread(name, fn, 0x10000100, stack_size, 0, 0, NULL);
    if (thread < 0)
        return thread;
    sceKernelDeleteThread(thread);
    return 0;
}

int main(void) {
    PRINT("a stack of 0x1000", create("t", entry, 0x1000));
    PRINT("a stack of 0x100", create("t", entry, 0x100));
    PRINT("a stack that isn't 4 KiB aligned", create("t", entry, 0x1234));
    PRINT("an empty name", create("", entry, 0x4000));
    PRINT("no entry", create("t", NULL, 0x4000));

    test_exit();
    return 0;
}
