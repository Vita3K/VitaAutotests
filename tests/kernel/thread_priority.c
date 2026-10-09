// The priorities a game may create threads with: user game threads may use 0x40 to 0xBF, and
// priorities relative to the default from 0x100000E0 to 0x1000011F.
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Returns 0 if a thread can be created with the priority, or the error
static int create_with(int priority) {
    const SceUID thread = sceKernelCreateThread("priority", entry, priority, 0x4000, 0, 0, NULL);
    if (thread < 0)
        return thread;
    sceKernelDeleteThread(thread);
    return 0;
}

int main(void) {
    PRINT("the highest game priority", create_with(0x40));
    PRINT("the lowest game priority", create_with(0xBF));
    PRINT("above the highest game priority", create_with(0x3F));
    PRINT("below the lowest game priority", create_with(0xC0));
    PRINT("the default priority", create_with(0x10000100));
    PRINT("the highest relative priority", create_with(0x100000E0));
    PRINT("the lowest relative priority", create_with(0x1000011F));
    PRINT("above the highest relative priority", create_with(0x100000DF));
    PRINT("below the lowest relative priority", create_with(0x10000120));

    test_exit();
    return 0;
}
