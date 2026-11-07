// What a mutex create gives for an option struct pointer to memory that isn't mapped. Each case
// creates one mutex or lightweight mutex from the main thread and prints the result. A created one
// is deleted at once. The cases pass the option pointer 0x4, then the address of a freed memory
// block, to:
// - a mutex create without attributes
// - a mutex create with the ceiling attribute
// - a mutex create with an init count of 2 and no recursive attribute, which a create also refuses
// - a lightweight mutex create without attributes
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static SceKernelLwMutexWork work __attribute__((aligned(8)));

static void run(const char *where, void *opt) {
    SceUID mutex = sceKernelCreateMutex("m", 0, 0, opt);
    PRINTF(test_created(mutex), "%s: a mutex create", where);
    if (mutex >= 0)
        sceKernelDeleteMutex(mutex);
    mutex = sceKernelCreateMutex("m", SCE_KERNEL_MUTEX_ATTR_CEILING, 0, opt);
    PRINTF(test_created(mutex), "%s: a mutex create with the ceiling attribute", where);
    if (mutex >= 0)
        sceKernelDeleteMutex(mutex);
    mutex = sceKernelCreateMutex("m", 0, 2, opt);
    PRINTF(test_created(mutex), "%s: a mutex create with an init count of 2", where);
    if (mutex >= 0)
        sceKernelDeleteMutex(mutex);
    const int lw = sceKernelCreateLwMutex(&work, "m", 0, 0, opt);
    PRINTF(lw, "%s: a lightweight mutex create", where);
    if (lw >= 0)
        sceKernelDeleteLwMutex(&work);
}

int main(void) {
    run("option pointer 0x4", (void *)0x4);

    const SceUID block = sceKernelAllocMemBlock("freed", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x1000, NULL);
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    sceKernelFreeMemBlock(block);
    run("a freed block", base);

    test_exit();
    return 0;
}
