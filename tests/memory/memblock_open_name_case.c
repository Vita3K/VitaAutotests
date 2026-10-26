// Whether sceKernelOpenMemBlock matches names case sensitively. Each block is 64 KiB of USER_RW
// with ENABLE_OPEN, holding a word of its own, and an open with flags 0x10 shows which block it
// maps by that word: 1 for the first one made, 2 for the second, and 0 when the open fails or
// neither word is there. The cases:
// - two blocks named "Case" and "case", opened by "CASE", by "case" and by "Case"
// - two blocks with 31 character names that differ only in the case of their letters, opened by
//   each name
// Each make and each open prints its result, and each open also prints which block it maps. The
// blocks are freed at the end.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define ENABLE_OPEN 0x4000
#define FIRST_WORD 0x11111111
#define SECOND_WORD 0x22222222

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

static volatile SceUInt32 *base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return base;
}

// Makes an ENABLE_OPEN block of the name, holding word at its start
static SceUID make(const char *name, SceUInt32 word) {
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    const SceUID block = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 64 * KIB,
                                                (SceKernelAllocMemBlockOpt *)&opt);
    if (block >= 0)
        base_of(block)[0] = word;
    return block;
}

// Opens the name with 0x10, prints the result and which block it maps, then closes it
static void open_which(const char *label, const char *name) {
    const SceUID opened = sceKernelOpenMemBlock(name, 0x10);
    PRINTF(test_created(opened), "%s: the result", label);
    int which = 0;
    if (opened >= 0) {
        volatile SceUInt32 *theirs = base_of(opened);
        if (theirs && sceKernelFindMemBlockByAddr((void *)theirs, 0) == opened) {
            if (theirs[0] == FIRST_WORD)
                which = 1;
            else if (theirs[0] == SECOND_WORD)
                which = 2;
        }
        sceKernelCloseMemBlock(opened);
    }
    PRINTF(which, "%s: the block it maps", label);
}

int main(void) {
    const SceUID lower = make("Case", FIRST_WORD);
    PRINT("make the block named Case", test_created(lower));
    const SceUID upper = make("case", SECOND_WORD);
    PRINT("make the block named case", test_created(upper));
    open_which("open CASE", "CASE");
    open_which("open case", "case");
    open_which("open Case", "Case");
    if (upper >= 0)
        sceKernelFreeMemBlock(upper);
    if (lower >= 0)
        sceKernelFreeMemBlock(lower);

    const SceUID long_upper = make("ABCDEFGHIJKLMNOPQRSTUVWXYZ01234", FIRST_WORD);
    PRINT("make the block with the 31 character upper case name", test_created(long_upper));
    const SceUID long_lower = make("abcdefghijklmnopqrstuvwxyz01234", SECOND_WORD);
    PRINT("make the block with the 31 character lower case name", test_created(long_lower));
    open_which("open the upper case name", "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234");
    open_which("open the lower case name", "abcdefghijklmnopqrstuvwxyz01234");
    if (long_lower >= 0)
        sceKernelFreeMemBlock(long_lower);
    if (long_upper >= 0)
        sceKernelFreeMemBlock(long_upper);

    test_exit();
    return 0;
}
