// Calls whose size word says 0 bytes, with the struct at the start of a page. The struct is the
// base of a USER_RW memory block, which is always page aligned. The cases:
// - sceKernelCreateThread with an option of size 0, then the same thread is deleted
// - sceKernelGetThreadContextForVM and sceKernelSetThreadContextForVM on an unknown UID, with a
//   CPU block of size 0
//
// On a PS Vita TV on firmware 3.65 the thread create accepts the empty option and returns a thread,
// and both ForVM calls give the unknown UID error 0x80028021, so a size of 0 at a page start is not
// refused as unreadable.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int sceKernelSetThreadContextForVM(SceUID thid, const ThreadCpuRegisters *cpu, const ThreadVfpRegisters *vfp);

static int dummy_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

int main(void) {
    const SceUID block = sceKernelAllocMemBlock("size zero", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x10000, NULL);
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);

    SceKernelThreadOptParam *const option = base;
    option->size = 0;
    const SceUID thread = sceKernelCreateThread("size zero", dummy_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, option);
    PRINT("thread: an option of size 0 at a page start", test_created(thread));
    if (thread >= 0)
        sceKernelDeleteThread(thread);

    ThreadCpuRegisters *const cpu = base;
    cpu->size = 0;
    PRINT("Get, a CPU block of size 0 at a page start, an unknown UID", sceKernelGetThreadContextForVM(0x12345, cpu, NULL));
    cpu->size = 0;
    PRINT("Set, a CPU block of size 0 at a page start, an unknown UID", sceKernelSetThreadContextForVM(0x12345, cpu, NULL));

    sceKernelFreeMemBlock(block);
    test_exit();
    return 0;
}
