// The order of the errors of sceKernelGetThreadContextForVM and sceKernelSetThreadContextForVM
// when the thread ID is bad and the register blocks are bad too. Each call is made on an unknown
// UID, UID 0, the calling thread, a semaphore and a thread without the VM attribute. Get gets a
// CPU block with size 0 and one with size 4 bytes too small, Set gets no blocks at all and a CPU
// block with size 0. A block 4 bytes too big is tried on an unknown UID and on the calling thread.
// It shows which check comes first, the UID or the size of the blocks. No thread runs, so every
// result is fixed.
//
// On a PS Vita TV on firmware 3.65 an unknown UID and UID 0 give 0x80028021 and the calling thread
// gives 0x80028022 for every call with a block size that is 0 or too small, or with no blocks. A
// semaphore and a thread without the VM attribute give 0x8002000B, except Set without blocks, which
// gives 0x80020006. A CPU block with a size 4 bytes too big gives 0x8002710B on an unknown UID and
// on the calling thread, for both Get and Set, so that size check comes before the UID check. A VFP
// block 4 bytes too small gives the UID error, 0x80028021 for an unknown UID and 0x80028022 for the
// calling thread.
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
    const SceUID sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    // Never started, so it stays dormant without the VM attribute
    const SceUID plain = sceKernelCreateThread("plain", dummy_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID self = sceKernelGetThreadId();
    const SceUID targets[] = { 0x12345, 0, self, sema, plain };
    const char *const names[] = { "an unknown UID", "UID 0", "the calling thread", "a semaphore", "a thread without the VM attribute" };

    ThreadCpuRegisters cpu = { .size = sizeof(cpu) };
    ThreadVfpRegisters vfp = { .size = sizeof(vfp) };
    for (int i = 0; i < 5; i++) {
        cpu.size = 0;
        PRINTF(sceKernelGetThreadContextForVM(targets[i], &cpu, NULL), "Get, CPU size 0, %s", names[i]);
        cpu.size = sizeof(cpu) - 4;
        PRINTF(sceKernelGetThreadContextForVM(targets[i], &cpu, NULL), "Get, CPU size 4 too small, %s", names[i]);
        PRINTF(sceKernelSetThreadContextForVM(targets[i], NULL, NULL), "Set, no blocks, %s", names[i]);
        cpu.size = 0;
        PRINTF(sceKernelSetThreadContextForVM(targets[i], &cpu, NULL), "Set, CPU size 0, %s", names[i]);
    }

    cpu.size = sizeof(cpu) + 4;
    PRINT("Get, CPU size 4 too big, an unknown UID", sceKernelGetThreadContextForVM(targets[0], &cpu, NULL));
    PRINT("Get, CPU size 4 too big, the calling thread", sceKernelGetThreadContextForVM(self, &cpu, NULL));
    PRINT("Set, CPU size 4 too big, an unknown UID", sceKernelSetThreadContextForVM(targets[0], &cpu, NULL));
    PRINT("Set, CPU size 4 too big, the calling thread", sceKernelSetThreadContextForVM(self, &cpu, NULL));
    cpu.size = sizeof(cpu);
    vfp.size = sizeof(vfp) - 4;
    PRINT("Get, VFP size 4 too small, an unknown UID", sceKernelGetThreadContextForVM(targets[0], &cpu, &vfp));
    PRINT("Get, VFP size 4 too small, the calling thread", sceKernelGetThreadContextForVM(self, &cpu, &vfp));

    sceKernelDeleteThread(plain);
    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
