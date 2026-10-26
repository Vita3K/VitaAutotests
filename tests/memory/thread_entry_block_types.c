// The blocks that sceKernelCreateThread and sceKernelCreateCallback take an entry address from.
// Each kind of memory below is given as the entry of a new thread and as the function of a new
// callback, and the result of each create call is printed. Nothing is started, and each object
// made is deleted at once. The cases:
// - app code, a function of this test, as the control
// - app data, a word of this test
// - the base of a 4 KiB USER_RW block, of a 4 KiB USER_RW_UNCACHE block, of a 256 KiB USER_CDRAM_RW
//   block and of a 4 KiB USER_MAIN_R block
// - the base of a 0x10 alias of an openable 4 KiB USER_RW block
// - the base of a 1 MiB block for code from sceKernelAllocMemBlockForVM, with the VM domain closed,
//   then open
// - the stack of a thread that is made and never started, and its TLS slot 0
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define ENABLE_OPEN 0x4000

// The option struct games pass, without the fields vitasdk adds after strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

// vitasdk has a stub for it but no prototype
int sceKernelFreeMemBlockForVM(SceUID uid);

static volatile int data_word = 1;

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Gives addr as the entry of a new thread and as the function of a new callback, prints both
// results and deletes what was made
static void both(const char *name, SceUIntPtr addr) {
    const SceUID thread = sceKernelCreateThread("entry", (SceKernelThreadEntry)addr,
        SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    PRINTF(test_created(thread), "%s: a thread with it as its entry", name);
    if (thread >= 0)
        sceKernelDeleteThread(thread);
    const SceUID callback
        = sceKernelCreateCallback("entry", 0, (SceKernelCallbackFunction)addr, NULL);
    PRINTF(test_created(callback), "%s: a callback with it as its function", name);
    if (callback >= 0)
        sceKernelDeleteCallback(callback);
}

// Makes a block of the type and size, gives its base to both calls, then frees it
static void block_case(const char *name, SceUInt32 type, SceSize size) {
    const SceUID block = sceKernelAllocMemBlock("entry block", type, size, NULL);
    PRINTF(test_created(block), "%s: allocate", name);
    void *base = NULL;
    if (block >= 0)
        sceKernelGetMemBlockBase(block, &base);
    both(name, (SceUIntPtr)base);
    if (block >= 0)
        sceKernelFreeMemBlock(block);
}

// Opens a 0x10 alias of an openable block, gives its base to both calls, then closes it and frees
// the original
static void alias_case(void) {
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    const SceUID original = sceKernelAllocMemBlock("entry openable",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, (SceKernelAllocMemBlockOpt *)&opt);
    PRINTF(test_created(original), "openable USER_RW: allocate");
    const SceUID alias = sceKernelOpenMemBlock("entry openable", 0x10);
    void *base = NULL;
    if (alias >= 0)
        sceKernelGetMemBlockBase(alias, &base);
    PRINTF(test_created(alias), "0x10 alias: open");
    both("0x10 alias", (SceUIntPtr)base);
    if (alias >= 0)
        sceKernelCloseMemBlock(alias);
    if (original >= 0)
        sceKernelFreeMemBlock(original);
}

// Makes a block for code, gives its base to both calls with the VM domain closed and then open
static void code_case(void) {
    const SceUID code = sceKernelAllocMemBlockForVM("entry code", MIB);
    PRINTF(test_created(code), "code block: allocate");
    void *base = NULL;
    if (code >= 0)
        sceKernelGetMemBlockBase(code, &base);
    both("code block, domain closed", (SceUIntPtr)base);
    PRINTF(sceKernelOpenVMDomain(), "code block: open the domain");
    both("code block, domain open", (SceUIntPtr)base);
    PRINTF(sceKernelCloseVMDomain(), "code block: close the domain");
    if (code >= 0)
        sceKernelFreeMemBlockForVM(code);
}

// Makes a thread that never starts, gives its stack and TLS slot 0 to both calls, then deletes it
static void dormant_case(void) {
    const SceUID thread = sceKernelCreateThread("entry dormant", entry,
        SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    PRINTF(test_created(thread), "dormant thread: create");
    if (thread < 0)
        return;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    both("dormant thread's stack", (SceUIntPtr)info.stack);
    both("dormant thread's TLS slot 0", (SceUIntPtr)sceKernelGetThreadTLSAddr(thread, 0));
    sceKernelDeleteThread(thread);
}

int main(void) {
    both("app code", (SceUIntPtr)entry);
    both("app data", (SceUIntPtr)&data_word);
    block_case("USER_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB);
    block_case("USER_RW_UNCACHE", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4 * KIB);
    block_case("USER_CDRAM_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB);
    block_case("USER_MAIN_R", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_R, 4 * KIB);
    alias_case();
    code_case();
    dormant_case();

    test_exit();
    return 0;
}
