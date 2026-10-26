// The UIDs memory blocks get. A freed block's UID is not given to the next block, to a new
// semaphore or to an alias opened after an alias was closed, and the top bits of each kind of
// block UID are checked. Only comparisons are printed, and no freed UID is ever called again. The
// cases:
// - a block made after a block was freed, and whether it has the freed UID
// - a semaphore made after a block was freed, and whether it has the freed UID
// - an alias opened with 0x10 after another alias was closed, and whether it has that alias's UID
//   or the UID of a block freed before it was opened
// - whether the UID of a plain block, an ENABLE_OPEN block, a USER_CDRAM_RW block, a
//   USER_MAIN_PHYCONT_RW block, an alias opened with 0x30 and a block for code is odd, and what
//   bits 31 and 30 hold
// - 16 plain blocks alive at once, all made, and whether their UIDs are distinct
// Every block is freed or closed at the end.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define ENABLE_OPEN 0x4000
#define LIVE 16

// The headers give no prototype for it, so it is declared here
int sceKernelFreeMemBlockForVM(SceUID uid);

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

// Prints whether the UID is odd, and whether bits 31 and 30 are 0 and 1
static void print_bits(const char *name, SceUID uid) {
    const SceUInt32 bits = (SceUInt32)uid;
    PRINTF((bits & 1) == 1, "%s: the UID is odd", name);
    PRINTF((bits >> 31) == 0, "%s: bit 31 of the UID is clear", name);
    PRINTF(((bits >> 30) & 1) == 1, "%s: bit 30 of the UID is set", name);
}

// Prints whether a block of the kind was made, and its UID's bits if it was
static void check_kind(const char *name, SceUID uid) {
    PRINTF(test_created(uid), "%s: allocate", name);
    if (uid >= 0)
        print_bits(name, uid);
}

int main(void) {
    // A freed block's UID, given to a block made after the free
    const SceUID freed =
        sceKernelAllocMemBlock("uid freed", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
    sceKernelFreeMemBlock(freed);
    const SceUID next =
        sceKernelAllocMemBlock("uid next", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
    PRINT("a block made after a free has the freed UID", next == freed);
    if (next >= 0)
        sceKernelFreeMemBlock(next);

    // A freed block's UID, given to a semaphore made after the free
    const SceUID sema_block =
        sceKernelAllocMemBlock("uid sema", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
    sceKernelFreeMemBlock(sema_block);
    const SceUID sema = sceKernelCreateSema("uid sema", 0, 0, 1, NULL);
    PRINT("a semaphore made after a free has the freed UID", sema == sema_block);
    if (sema >= 0)
        sceKernelDeleteSema(sema);

    // Aliases: an alias opened after a closed alias, and after a freed block
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    const SceUID openable = sceKernelAllocMemBlock("uid alias", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                                   4 * KIB, (SceKernelAllocMemBlockOpt *)&opt);
    PRINT("allocate an ENABLE_OPEN block for aliases", test_created(openable));
    const SceUID first = openable >= 0 ? sceKernelOpenMemBlock("uid alias", 0x10) : openable;
    PRINT("open an alias", test_created(first));
    if (first >= 0)
        sceKernelCloseMemBlock(first);
    const SceUID between =
        sceKernelAllocMemBlock("uid between", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
    sceKernelFreeMemBlock(between);
    const SceUID second = openable >= 0 ? sceKernelOpenMemBlock("uid alias", 0x10) : openable;
    PRINT("an alias opened after a closed alias has its UID", second == first);
    PRINT("an alias opened after a free has the freed block's UID", second == between);
    if (second >= 0)
        sceKernelCloseMemBlock(second);

    // The bits of each kind
    const SceUID plain =
        sceKernelAllocMemBlock("uid plain", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
    check_kind("plain block", plain);
    if (plain >= 0)
        sceKernelFreeMemBlock(plain);

    const SceUID enable = sceKernelAllocMemBlock("uid enable", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                                 4 * KIB, (SceKernelAllocMemBlockOpt *)&opt);
    check_kind("ENABLE_OPEN block", enable);

    const SceUID alias = openable >= 0 ? sceKernelOpenMemBlock("uid alias", 0x30) : openable;
    check_kind("alias opened with 0x30", alias);
    if (alias >= 0)
        sceKernelCloseMemBlock(alias);
    if (enable >= 0)
        sceKernelFreeMemBlock(enable);

    const SceUID cdram = sceKernelAllocMemBlock("uid cdram", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                                256 * KIB, NULL);
    check_kind("USER_CDRAM_RW block", cdram);
    if (cdram >= 0)
        sceKernelFreeMemBlock(cdram);

    const SceUID phycont = sceKernelAllocMemBlock(
        "uid phycont", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, MIB, NULL);
    check_kind("USER_MAIN_PHYCONT_RW block", phycont);
    if (phycont >= 0)
        sceKernelFreeMemBlock(phycont);

    const SceUID code = sceKernelAllocMemBlockForVM("uid code", MIB);
    check_kind("block for code", code);
    if (code >= 0)
        sceKernelFreeMemBlockForVM(code);

    if (openable >= 0)
        sceKernelFreeMemBlock(openable);

    // Sixteen live blocks at once
    SceUID live[LIVE];
    int made = 1;
    for (int i = 0; i < LIVE; i++) {
        live[i] =
            sceKernelAllocMemBlock("uid live", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
        made &= live[i] >= 0;
    }
    PRINT("16 live blocks are all made", made);
    int distinct = 1;
    for (int i = 0; i < LIVE; i++)
        for (int j = i + 1; j < LIVE; j++)
            if (live[i] == live[j])
                distinct = 0;
    PRINT("16 live blocks have distinct UIDs", distinct);
    for (int i = 0; i < LIVE; i++)
        if (live[i] >= 0)
            sceKernelFreeMemBlock(live[i]);

    test_exit();
    return 0;
}
