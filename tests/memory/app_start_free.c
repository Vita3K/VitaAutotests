// The main free size an app sees when it starts, next to the blocks the app already holds. The test
// reads sceKernelGetFreeMemorySize first thing. Then it measures the exact free size F to 4 KiB
// with 4 KiB USER_RW probes, as app_free_sizes does, adding back the 2 MiB that the reported size
// keeps out, and reads the mapped sizes of the blocks behind the app's code, data, heap, main thread
// stack and main thread TLS. The rest of the 256 MiB, which is what the app didn't ask for itself,
// is the free size and those five blocks taken from 0x10000000. The test prints:
// - whether the reported size is F rounded down to a whole MiB, less 2 MiB, and whether it is a
//   whole number of MiB
// - the size of the heap and of the TLS block
// - whether the rest is a whole number of pages, whether it is at least 0x313000, and whether it is
//   under 1 MiB more than that
// The reported size itself and the code, data and stack sizes change with the build, so the test
// prints only whether the reported size is between 96 MiB and 127 MiB.
#include <stdlib.h>

#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define PROBES 320

static SceUID probes[PROBES];
static volatile int data_word = 1;

static int reported(void) {
    SceKernelFreeMemorySizeInfo sizes = { .size = sizeof(sizes) };
    sceKernelGetFreeMemorySize(&sizes);
    return sizes.size_user;
}

// The main free size to 4 KiB, or -1 if it never drops
static int measure(void) {
    const int start = reported();
    int made = 0, now = start;
    while (made < PROBES && now == start) {
        const SceUID block = sceKernelAllocMemBlock("free probe", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
        if (block < 0)
            break;
        probes[made++] = block;
        now = reported();
    }
    for (int i = 0; i < made; i++)
        sceKernelFreeMemBlock(probes[i]);
    return now == start ? -1 : start + (made - 1) * 4 * KIB;
}

static unsigned block_size(const void *addr) {
    SceKernelMemBlockInfo info = { .size = sizeof(info) };
    return sceKernelGetMemBlockInfoByAddr((void *)addr, &info) < 0 ? 0 : info.mappedSize;
}

int main(void) {
    const unsigned first = (unsigned)reported();
    const int measured = measure();
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    void *heap = malloc(16);
    const unsigned code = block_size((const void *)((SceUIntPtr)main & ~(SceUIntPtr)1));
    const unsigned data = block_size((const void *)&data_word);
    const unsigned heap_size = block_size(heap);
    const unsigned stack = block_size(info.stack);
    const unsigned tls = block_size(sceKernelGetTLSAddr(0));

    const unsigned free_size = (unsigned)measured + 2 * MIB;
    const unsigned rest = 0x10000000U - free_size - code - data - heap_size - stack - tls;

    PRINT("the measure found the size", measured >= 0);
    PRINT("the blocks of the app's code, data, heap, stack and TLS are found", code && data && heap_size && stack && tls);
    PRINT("the reported size is the measured one rounded down to a MiB, less 2 MiB", first == (free_size & 0xFFF00000U) - 2 * MIB);
    PRINT("the reported size is a whole number of MiB", (first & 0xFFFFF) == 0);
    PRINT("the reported size is between 96 MiB and 127 MiB", first >= 96 * MIB && first <= 127 * MIB);
    PRINT("the heap block size", heap_size);
    PRINT("the TLS block size", tls);
    PRINT("the rest is a whole number of pages", (rest & 0xFFF) == 0);
    PRINT("the rest is at least 0x313000", rest >= 0x313000);
    PRINT("the rest is under 1 MiB above 0x313000", rest < 0x313000 + MIB);

    free(heap);
    test_exit();
    return 0;
}
