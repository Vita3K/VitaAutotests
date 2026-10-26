// Blocks for code from sceKernelAllocMemBlockForVM that are refused, and
// whether a refused one leaves main memory as it was. Then a 16 MiB block,
// freed with sceKernelFreeMemBlockForVM at once, and a 1 MiB one, freed the
// same way, to see whether both fit after the refusals. The sizes that a wrong
// implementation may crash on, 2 GiB and 4095 MiB, come last. The cases:
// - 0 bytes
// - 1 MiB + 4 KiB, which is not a whole number of MiB
// - 17 MiB
// - 16 MiB, freed at once, then 1 MiB, freed at once
// - 2 GiB, then 4095 MiB
// Each refused size prints its error, and whether the main free size is
// unchanged around the call, measured to the page the way app_free_sizes does.
// A block that a refused size does get is freed with
// sceKernelFreeMemBlockForVM, so the rest of the test still starts from the
// same space.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB ((SceSize)0x100000)
#define PROBES 320

// The stub library has it, but the headers have no prototype
int sceKernelFreeMemBlockForVM(SceUID uid);

static SceUID probes[PROBES];

static int reported_user(void) {
    SceKernelFreeMemorySizeInfo sizes = { .size = sizeof(sizes) };
    sceKernelGetFreeMemorySize(&sizes);
    return sizes.size_user;
}

// The free size of main memory to the page, or -1 if it never drops
static int measure(void) {
    const int start = reported_user();
    int made = 0, now = start;
    while (made < PROBES && now == start) {
        const SceUID block =
            sceKernelAllocMemBlock("free probe", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
        if (block < 0)
            break;
        probes[made++] = block;
        now = reported_user();
    }
    for (int i = 0; i < made; i++)
        sceKernelFreeMemBlock(probes[i]);
    return now == start ? -1 : start + (made - 1) * (int)(4 * KIB);
}

// Tries a block for code of the size, prints its error, and whether main memory
// is unchanged
static void refused(SceSize size) {
    const int before = measure();
    const SceUID block = sceKernelAllocMemBlockForVM("vm refused", size);
    const int after = measure();
    PRINTF(test_created(block), "allocate 0x%X bytes", (unsigned)size);
    PRINTF(before == after, "allocate 0x%X bytes: the main free size is unchanged", (unsigned)size);
    if (block >= 0)
        sceKernelFreeMemBlockForVM(block);
}

int main(void) {
    // The first measure only warms up
    measure();

    refused(0);
    refused(MIB + 4 * KIB);
    refused(17 * MIB);

    SceUID block = sceKernelAllocMemBlockForVM("vm fit", 16 * MIB);
    PRINT("allocate 16 MiB", test_created(block));
    if (block >= 0)
        PRINT("free it at once", sceKernelFreeMemBlockForVM(block));

    block = sceKernelAllocMemBlockForVM("vm fit", MIB);
    PRINT("allocate 1 MiB after that", test_created(block));
    if (block >= 0)
        PRINT("free it", sceKernelFreeMemBlockForVM(block));

    refused(2048 * MIB);
    refused(4095 * MIB);

    test_exit();
    return 0;
}
