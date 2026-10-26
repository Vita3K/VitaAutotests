// The largest stacks a thread can be created with. A 1 MiB stack is created as a control, then the
// sizes above 0x2000000 are asked for, which are refused for a program that declares 3.00 or
// later. A create that succeeds is deleted again at once. The refused sizes come after the
// control, since a wrong implementation may crash or hang on them and then loses only the lines
// after the control.
// Cases: a 1 MiB control, then 0x2000001, 0x7FFFFFFF, 0x80000000 and 0xFFFFF001. Each prints the
// result of its create, 0 when it made a thread.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

int main(void) {
    const SceSize stacks[] = { 0x100000, 0x2000001, 0x7FFFFFFF, 0x80000000, 0xFFFFF001 };
    for (int i = 0; i < (int)(sizeof(stacks) / sizeof(stacks[0])); i++) {
        const SceUID thread =
            sceKernelCreateThread("stack", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, stacks[i], 0, 0, NULL);
        PRINTF(test_created(thread), "stack 0x%X", (unsigned)stacks[i]);
        if (thread >= 0)
            PRINTF(sceKernelDeleteThread(thread), "delete of stack 0x%X", (unsigned)stacks[i]);
    }

    test_exit();
    return 0;
}
