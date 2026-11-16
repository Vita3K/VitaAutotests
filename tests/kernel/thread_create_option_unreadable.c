// sceKernelCreateThread given an option pointer that can't be read, alone and together with
// another bad argument, to see which error comes first. The option pointer is the base of a
// USER_RW block freed right before the call. Each case prints the create's result and deletes the
// thread if one was made anyway. unmapped_in already covers the option alone. The cases:
// - the option alone
// - the option and a NULL name
// - the option and a NULL entry
// - the option and the priority above the highest game priority
// - the option and a negative stack size
//
// On a PS Vita TV on firmware 3.65 every case fails with 0x80022005: the thread create reads its
// option before the name, the entry, the priority and the stack size.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Allocates a block and frees it, and returns its base, which is in no block until the next
// allocation
static void *unmapped(void) {
    const SceUID block
        = sceKernelAllocMemBlock("option unreadable", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x10000,
            NULL);
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    sceKernelFreeMemBlock(block);
    return base;
}

static void create(const char *where, const char *name, SceKernelThreadEntry fn, int prio,
    SceSize stack) {
    const SceUID thread = sceKernelCreateThread(name, fn, prio, stack, 0, 0, unmapped());
    PRINTF(test_created(thread), "thread: %s", where);
    if (thread >= 0)
        sceKernelDeleteThread(thread);
}

int main(void) {
    const int prio = SCE_KERNEL_DEFAULT_PRIORITY_USER;
    create("the option alone", "option unreadable", entry, prio, 0x4000);
    create("the option and a NULL name", NULL, entry, prio, 0x4000);
    create("the option and a NULL entry", "option unreadable", NULL, prio, 0x4000);
    create("the option and the priority above the highest game priority", "option unreadable",
        entry, SCE_KERNEL_HIGHEST_PRIORITY_USER - 1, 0x4000);
    create("the option and a negative stack size", "option unreadable", entry, prio,
        (SceSize)-1);

    test_exit();
    return 0;
}
