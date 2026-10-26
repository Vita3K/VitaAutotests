// Names of blocks for code, next to an ENABLE_OPEN block with the same name. Models:
// memblock_for_vm_refree (the blocks for code) and memblock_open_names (which block an open maps).
// Two 1 MiB blocks for code are both named "vm dup" and kept live. Then a 64 KiB USER_RW block with
// ENABLE_OPEN under the same name is made, with a word written in it, and the name is opened with
// 0x10. The cases:
// - the first and second block for code with the name, each one's result
// - the ENABLE_OPEN block with the name, its result
// - the open with 0x10, its result, and whether it maps the ENABLE_OPEN block by the word in it
// - the open is closed, the ENABLE_OPEN block is freed, then both blocks for code are freed with
//   the call for code
//
// If the second block for code is refused, that's printed and the test goes on. The word is only
// read through an opened UID that the lookup by its base gives back, and a block for code is never
// read.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define ENABLE_OPEN 0x4000
#define WORD 0x11111111

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

// The call for code's free, which the shared declarations lack
int sceKernelFreeMemBlockForVM(SceUID uid);

static volatile SceUInt32 *base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return base;
}

// Whether the opened UID maps the word, as found by the lookup by its base
static int maps_word(SceUID opened) {
    volatile SceUInt32 *words = base_of(opened);
    return words && sceKernelFindMemBlockByAddr((void *)words, 0) == opened && words[0] == WORD;
}

int main(void) {
    const SceUID first = sceKernelAllocMemBlockForVM("vm dup", MIB);
    PRINT("the first block for code named vm dup", test_created(first));
    const SceUID second = sceKernelAllocMemBlockForVM("vm dup", MIB);
    PRINT("the second block for code named vm dup", test_created(second));

    AllocOpt opt = {.size = sizeof(opt), .attr = ENABLE_OPEN};
    const SceUID block = sceKernelAllocMemBlock("vm dup", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                                64 * KIB, (SceKernelAllocMemBlockOpt *)&opt);
    PRINT("the 64 KiB ENABLE_OPEN block named vm dup", test_created(block));
    if (block >= 0)
        base_of(block)[0] = WORD;

    const SceUID opened = sceKernelOpenMemBlock("vm dup", 0x10);
    PRINT("open vm dup with 0x10", test_created(opened));
    if (opened >= 0)
        PRINT("open vm dup: it maps the ENABLE_OPEN block", maps_word(opened));
    if (opened >= 0)
        PRINT("close the opened block", sceKernelCloseMemBlock(opened));
    if (block >= 0)
        PRINT("free the ENABLE_OPEN block", sceKernelFreeMemBlock(block));
    if (second >= 0)
        PRINT("free the second block for code", sceKernelFreeMemBlockForVM(second));
    if (first >= 0)
        PRINT("free the first block for code", sceKernelFreeMemBlockForVM(first));
    test_exit();
    return 0;
}
