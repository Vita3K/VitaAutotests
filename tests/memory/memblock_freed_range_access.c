// What sceKernelFindMemBlockByAddr gives at a memory block's old range after the block went away,
// and whether an alias or its original keeps the range while the other one lives. Each find is
// made right after the call that ends the block, with nothing allocated in between. The kernel
// probes of the ranges, which write and read through the pointers they're given, are in
// memblock_freed_range_probe, since a wrong implementation may crash on them.
//
// The cases:
// - a block of USER_RW_UNCACHE, CDRAM, PHYCONT, and of the cached and the uncached common dialog
//   types, freed with the usual free
// - a 0x30 alias closed, then freed, while its original lives: the alias's range, then the
//   original's range, then the original freed and its range again
// - the original closed, then freed, while a 0x30 alias lives: the original's range, the alias's
//   range, the alias's base by its UID, the alias closed and its range again
// - a block for code freed with sceKernelFreeMemBlockForVM, then one freed with the usual free,
//   then the space of the second one given back with the call for code
// - a dormant thread's stack and its TLS slot 0 before and after sceKernelDeleteThread
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define ENABLE_OPEN 0x4000
#define ALIAS_FLAGS 0x30

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

// The stub library has it, but the headers have no prototype
int sceKernelFreeMemBlockForVM(SceUID uid);

static char *base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return base;
}

// Returns 0 when the block is found at base, the error when no block is, and 1 for another block
static int find_result(const char *base, SceUID block) {
    const SceUID found = sceKernelFindMemBlockByAddr(base, 0);
    return found == block ? 0 : found < 0 ? found : 1;
}

// Returns 0 when any block is found at base, or the error
static int finds(const void *base) {
    const SceUID found = sceKernelFindMemBlockByAddr(base, 0);
    return found < 0 ? found : 0;
}

// Allocates a block of the type and size, ends it with the usual free and finds its range
static void freed(const char *how, SceUInt32 type, SceSize size) {
    const SceUID block = sceKernelAllocMemBlock(how, type, size, NULL);
    PRINTF(test_created(block), "%s: allocate", how);
    if (block < 0)
        return;
    char *base = base_of(block);
    PRINTF(sceKernelFreeMemBlock(block), "%s: free it", how);
    PRINTF(find_result(base, block), "%s, its range: find", how);
}

static SceUID alloc_openable(const char *name) {
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    return sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 64 * KIB,
        (SceKernelAllocMemBlockOpt *)&opt);
}

// Ends the 0x30 alias of a block that lives with end, then finds the alias's range and the
// original's, then frees the original and finds its range again
static void alias_ended(const char *how, int (*end)(SceUID)) {
    const SceUID original = alloc_openable(how);
    PRINTF(test_created(original), "%s: allocate the original", how);
    if (original < 0)
        return;
    char *base = base_of(original);
    const SceUID alias = sceKernelOpenMemBlock(how, ALIAS_FLAGS);
    PRINTF(test_created(alias), "%s: open the alias", how);
    if (alias < 0) {
        sceKernelFreeMemBlock(original);
        return;
    }
    char *alias_base = base_of(alias);
    PRINTF(end(alias), "%s: end the alias", how);
    PRINTF(find_result(alias_base, alias), "%s, the alias: find", how);
    PRINTF(find_result(base, original), "%s, the original: find", how);
    PRINTF(sceKernelFreeMemBlock(original), "%s: free the original", how);
    PRINTF(find_result(base, original), "%s, the original after its free: find", how);
}

// Ends the original with end while a 0x30 alias of it lives, finds both ranges, then gets the
// alias's base by its UID, closes the alias and finds its range again
static void original_ended(const char *how, int (*end)(SceUID)) {
    const SceUID original = alloc_openable(how);
    PRINTF(test_created(original), "%s: allocate the original", how);
    if (original < 0)
        return;
    char *base = base_of(original);
    const SceUID alias = sceKernelOpenMemBlock(how, ALIAS_FLAGS);
    PRINTF(test_created(alias), "%s: open the alias", how);
    if (alias < 0) {
        sceKernelFreeMemBlock(original);
        return;
    }
    char *alias_base = base_of(alias);
    PRINTF(end(original), "%s: end the original", how);
    PRINTF(find_result(base, original), "%s, the original: find", how);
    PRINTF(find_result(alias_base, alias), "%s, the alias: find", how);
    void *out = NULL;
    PRINTF(sceKernelGetMemBlockBase(alias, &out), "%s: the alias's base by its UID", how);
    PRINTF(sceKernelCloseMemBlock(alias), "%s: close the alias", how);
    PRINTF(find_result(alias_base, alias), "%s, the alias after its close: find", how);
}

// Makes a block for code, ends it with the call for code, or with the usual free when usual is
// set, then finds its range. A block the usual free ends keeps its share of the process, so the
// call for code gives that back at the end.
static void code_ended(const char *how, int usual) {
    const SceUID block = sceKernelAllocMemBlockForVM(how, MIB);
    PRINTF(test_created(block), "%s: allocate 1 MiB for code", how);
    if (block < 0)
        return;
    char *base = base_of(block);
    if (usual)
        PRINTF(sceKernelFreeMemBlock(block), "%s: free it with the usual free", how);
    else
        PRINTF(sceKernelFreeMemBlockForVM(block), "%s: free it with the call for code", how);
    PRINTF(find_result(base, block), "%s, its range: find", how);
    if (usual)
        PRINTF(sceKernelFreeMemBlockForVM(block), "%s: the call for code after that", how);
}

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// A thread with a stack of 0x4000 that never starts: its stack and TLS slot 0 are found before the
// delete, then after it
static void thread_cases(void) {
    const SceUID thread = sceKernelCreateThread("freed stack", entry,
        SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    PRINT("dormant thread: create", test_created(thread));
    if (thread < 0)
        return;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    const void *stack = info.stack;
    const void *tls = sceKernelGetThreadTLSAddr(thread, 0);
    PRINT("dormant thread stack before the delete: find", finds(stack));
    PRINT("the TLS slot 0 before the delete: find", finds(tls));

    PRINT("delete the thread", sceKernelDeleteThread(thread));
    PRINT("dormant thread stack after the delete: find", finds(stack));
    PRINT("the TLS slot 0 after the delete: find", finds(tls));
}

int main(void) {
    freed("USER_RW_UNCACHE", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 64 * KIB);
    freed("CDRAM", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB);
    freed("PHYCONT", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, MIB);
    freed("cached common dialog", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_RW, 64 * KIB);
    freed("uncached common dialog", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_NC_RW, 64 * KIB);

    alias_ended("alias closed", sceKernelCloseMemBlock);
    alias_ended("alias freed", sceKernelFreeMemBlock);
    original_ended("original closed", sceKernelCloseMemBlock);
    original_ended("original freed", sceKernelFreeMemBlock);

    code_ended("code freed for code", 0);
    code_ended("code freed the usual way", 1);

    thread_cases();
    test_exit();
    return 0;
}
