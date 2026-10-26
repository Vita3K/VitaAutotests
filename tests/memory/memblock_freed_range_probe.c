// What kernel writes and reads do at a memory block's old range after the block went away. Each
// range is probed right after the call that ends the block, with nothing allocated in between. A
// probe is a call that writes a result through the pointer it's given, and a call that reads a name
// from one, so the app itself never touches the range. Each probe prints the find by address, the
// read and then the write, in that order.
//
// The read probe creates a semaphore whose name is the letter "a" at the range's second to last
// byte, followed by a zero byte, then deletes it. Its result is 0 when the kernel could read the
// name. The write probe has the kernel write the base of a block the test holds into the range's
// last word, and its result is 0 when the kernel could write there. When it could, the test puts
// the word back with a store of its own. Every range's last word is set while the test holds the
// block, before it is ended.
//
// The finds of the same ranges are in memblock_freed_range_access. This test is kept apart, since
// a wrong implementation may crash on a probe of a range that no block maps, and this is the only
// test that makes those probes.
//
// The cases:
// - a block of USER_RW_UNCACHE, CDRAM, PHYCONT, and of the cached and the uncached common dialog
//   types, freed with the usual free
// - a 0x30 alias closed, then freed, while its original lives: the alias's range, the original's
//   range as a control, then the original freed and its range again
// - the original closed, then freed, while a 0x30 alias lives: the original's range, the alias's
//   range, the alias's base by its UID, the alias closed and its range again
// - a block for code freed with sceKernelFreeMemBlockForVM, then one freed with the usual free
// - a dormant thread's stack before and after sceKernelDeleteThread, and its TLS slot 0 by find
//   only, since a write into a thread's TLS isn't probed
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define ENABLE_OPEN 0x4000
#define ALIAS_FLAGS 0x30

// The last word of a range: the byte before the last is 'a' and the last byte is zero
#define LAST_WORD 0x00610000

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

// A live block the write probes take their base from, made first and freed last
static SceUID scratch;

static char *base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return base;
}

// Sets the last word of a range the test holds
static void set_last_word(char *base, SceSize size) {
    *(volatile SceUInt32 *)(base + size - 4) = LAST_WORD;
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

// Returns 0 when the kernel could read the name at addr, or the error. A name it reads is made
// into a semaphore, which is deleted at once.
static int read_probe(const char *addr) {
    const SceUID sema = sceKernelCreateSema(addr, 0, 0, 1, NULL);
    if (sema < 0)
        return sema;
    sceKernelDeleteSema(sema);
    return 0;
}

// Returns 0 when the kernel could write the base of scratch to word, or the error
static int write_probe(void *word) {
    return sceKernelGetMemBlockBase(scratch, (void **)word);
}

// Prints the find, the read probe and the write probe of a range of size bytes at base, labeled
// how, part. The find result is taken by the caller right before this, and the read comes before
// the write, since the write changes the last word.
static void probe(const char *how, const char *part, int found, char *base, SceSize size) {
    PRINTF(found, "%s, %s: find", how, part);
    PRINTF(read_probe(base + size - 2), "%s, %s: read", how, part);
    void *const word = base + size - 4;
    const int written = write_probe(word);
    PRINTF(written, "%s, %s: write", how, part);
    if (written == 0)
        *(volatile SceUInt32 *)word = LAST_WORD;
}

// Allocates a block of the type and size, ends it with the usual free and probes its range
static void freed(const char *how, SceUInt32 type, SceSize size) {
    const SceUID block = sceKernelAllocMemBlock(how, type, size, NULL);
    PRINTF(test_created(block), "%s: allocate", how);
    if (block < 0)
        return;
    char *base = base_of(block);
    set_last_word(base, size);
    PRINTF(sceKernelFreeMemBlock(block), "%s: free it", how);
    probe(how, "its range", find_result(base, block), base, size);
}

static SceUID alloc_openable(const char *name) {
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    return sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 64 * KIB,
        (SceKernelAllocMemBlockOpt *)&opt);
}

// Ends the 0x30 alias of a block that lives with end, then probes the alias's range, then frees
// the original and probes its range
static void alias_ended(const char *how, int (*end)(SceUID)) {
    const SceUID original = alloc_openable(how);
    PRINTF(test_created(original), "%s: allocate the original", how);
    if (original < 0)
        return;
    char *base = base_of(original);
    set_last_word(base, 64 * KIB);
    const SceUID alias = sceKernelOpenMemBlock(how, ALIAS_FLAGS);
    PRINTF(test_created(alias), "%s: open the alias", how);
    if (alias < 0) {
        sceKernelFreeMemBlock(original);
        return;
    }
    char *alias_base = base_of(alias);
    PRINTF(end(alias), "%s: end the alias", how);
    probe(how, "the alias", find_result(alias_base, alias), alias_base, 64 * KIB);
    probe(how, "the original, a control", find_result(base, original), base, 64 * KIB);
    PRINTF(sceKernelFreeMemBlock(original), "%s: free the original", how);
    probe(how, "the original after its free", find_result(base, original), base, 64 * KIB);
}

// Ends the original with end while a 0x30 alias of it lives, probes both ranges, then gets the
// alias's base by its UID, closes the alias and probes its range again
static void original_ended(const char *how, int (*end)(SceUID)) {
    const SceUID original = alloc_openable(how);
    PRINTF(test_created(original), "%s: allocate the original", how);
    if (original < 0)
        return;
    char *base = base_of(original);
    set_last_word(base, 64 * KIB);
    const SceUID alias = sceKernelOpenMemBlock(how, ALIAS_FLAGS);
    PRINTF(test_created(alias), "%s: open the alias", how);
    if (alias < 0) {
        sceKernelFreeMemBlock(original);
        return;
    }
    char *alias_base = base_of(alias);
    PRINTF(end(original), "%s: end the original", how);
    probe(how, "the original", find_result(base, original), base, 64 * KIB);
    probe(how, "the alias", find_result(alias_base, alias), alias_base, 64 * KIB);
    void *out = NULL;
    PRINTF(sceKernelGetMemBlockBase(alias, &out), "%s: the alias's base by its UID", how);
    PRINTF(sceKernelCloseMemBlock(alias), "%s: close the alias", how);
    probe(how, "the alias after its close", find_result(alias_base, alias), alias_base, 64 * KIB);
}

// Makes a 1 MiB block for code and sets its last word with the domain open, since code blocks are
// only writable then
static void set_code_word(char *base) {
    sceKernelOpenVMDomain();
    set_last_word(base, MIB);
    sceKernelCloseVMDomain();
}

// Makes a block for code, ends it with the call for code, or with the usual free when usual is
// set, then probes its range
static void code_ended(const char *how, int usual) {
    const SceUID block = sceKernelAllocMemBlockForVM(how, MIB);
    PRINTF(test_created(block), "%s: allocate 1 MiB for code", how);
    if (block < 0)
        return;
    char *base = base_of(block);
    set_code_word(base);
    if (usual)
        PRINTF(sceKernelFreeMemBlock(block), "%s: free it with the usual free", how);
    else
        PRINTF(sceKernelFreeMemBlockForVM(block), "%s: free it with the call for code", how);
    probe(how, "its range", find_result(base, block), base, MIB);
    if (usual)
        PRINTF(sceKernelFreeMemBlockForVM(block), "%s: the call for code after that", how);
}

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// A thread with a stack of 0x4000 that never starts: its stack is probed after the delete, and its
// TLS slot 0 is found before and after
static void thread_cases(void) {
    const SceUID thread = sceKernelCreateThread("freed stack", entry,
        SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    PRINT("dormant thread: create", test_created(thread));
    if (thread < 0)
        return;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    char *stack = info.stack;
    const SceSize size = info.stackSize;
    const void *tls = sceKernelGetThreadTLSAddr(thread, 0);
    set_last_word(stack, size);
    probe("dormant thread stack", "before the delete", finds(stack), stack, size);
    PRINT("the TLS slot 0 before the delete: find", finds(tls));

    PRINT("delete the thread", sceKernelDeleteThread(thread));
    probe("dormant thread stack", "after the delete", finds(stack), stack, size);
    PRINT("the TLS slot 0 after the delete: find", finds(tls));
}

int main(void) {
    scratch = sceKernelAllocMemBlock("freed scratch", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB,
        NULL);
    PRINT("allocate the scratch block", test_created(scratch));
    if (scratch < 0) {
        test_exit();
        return 0;
    }

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

    PRINT("free the scratch block", sceKernelFreeMemBlock(scratch));
    test_exit();
    return 0;
}
