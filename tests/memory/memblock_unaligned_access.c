// Unaligned loads and stores of one, two and four bytes in a USER_RW block, with ldr, ldrh and str
// at an address one byte past a word. The block holds the bytes 0x00 to 0x0F. Each line prints what
// the access read, or the bytes the store left. The accesses are plain, so a core that faulted on
// them would end the app, and the output would stop before the line.
#include <psp2/kernel/sysmem.h>

#include "test.h"

int main(void) {
    const SceUID block = sceKernelAllocMemBlock("unaligned", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x1000, NULL);
    PRINT("allocate the block", test_created(block));
    unsigned char *base = NULL;
    sceKernelGetMemBlockBase(block, (void **)&base);
    for (int i = 0; i < 16; i++)
        base[i] = (unsigned char)i;

    unsigned int word, half, after;
    __asm__ volatile("ldr %0, [%1]" : "=r"(word) : "r"(base + 1) : "memory");
    PRINT("ldr at an address one byte past a word", word);
    __asm__ volatile("ldrh %0, [%1]" : "=r"(half) : "r"(base + 1) : "memory");
    PRINT("ldrh at an address one byte past a word", half);
    __asm__ volatile("str %0, [%1]" : : "r"(0xAABBCCDDU), "r"(base + 1) : "memory");
    after = base[0] | base[1] << 8 | base[2] << 16 | (unsigned)base[3] << 24;
    PRINT("str of 0xAABBCCDD one byte past a word: the bytes 0 to 3", after);
    after = base[4] | base[5] << 8 | base[6] << 16 | (unsigned)base[7] << 24;
    PRINT("str of 0xAABBCCDD one byte past a word: the bytes 4 to 7", after);

    sceKernelFreeMemBlock(block);
    test_exit();
    return 0;
}
