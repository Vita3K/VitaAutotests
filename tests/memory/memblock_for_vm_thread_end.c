// A block for code that a thread made and that outlives the thread. The thread allocates a 1 MiB
// block with sceKernelAllocMemBlockForVM, shares its UID and base, and ends. Main waits for the
// end, deletes the thread, and then does the following, each printed as it comes:
// - finds the block by its base, which should give the same UID back
// - tries a 16 MiB block for code while the 1 MiB one lives, freeing it at once if it is made
// - frees the 1 MiB block with sceKernelFreeMemBlockForVM
// - tries a 16 MiB block again, freeing it at once if it is made
// A find prints 0 when it gives the block, the error when it fails and 1 when it gives another
// UID. memblock_threads covers a block made by a thread that ends with the usual free, this test
// uses the blocks for code and their own free.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define MIB ((SceSize)0x100000)

// The stub library has it, but the headers have no prototype
int sceKernelFreeMemBlockForVM(SceUID uid);

// What the thread shares with main
static volatile SceUID shared_block;
static volatile SceUIntPtr shared_base;

static void *base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return base;
}

// Returns 0 if a find by the base gives the block, the error if it fails, and 1
// if it gives another
static int find_result(SceUIntPtr base, SceUID block) {
    const SceUID found = sceKernelFindMemBlockByAddr((const void *)base, 0);
    return found == block ? 0 : found < 0 ? found : 1;
}

static int alloc_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    shared_block = sceKernelAllocMemBlockForVM("vm thread", MIB);
    shared_base = shared_block >= 0 ? (SceUIntPtr)base_of(shared_block) : 0;
    return 0;
}

int main(void) {
    const SceUID thread = test_start_thread(alloc_entry);
    SceUInt timeout = TEST_WAIT_US;
    PRINT("wait for the thread to end", sceKernelWaitThreadEnd(thread, NULL, &timeout));
    PRINT("the thread's allocation", test_created(shared_block));
    PRINT("delete the thread", sceKernelDeleteThread(thread));
    if (shared_block < 0) {
        test_exit();
        return 0;
    }

    PRINT("find the block by its base", find_result(shared_base, shared_block));

    SceUID big = sceKernelAllocMemBlockForVM("vm main", 16 * MIB);
    PRINT("allocate 16 MiB while the 1 MiB block lives", test_created(big));
    if (big >= 0)
        PRINT("free that 16 MiB block", sceKernelFreeMemBlockForVM(big));

    PRINT("free the 1 MiB block", sceKernelFreeMemBlockForVM(shared_block));

    big = sceKernelAllocMemBlockForVM("vm main", 16 * MIB);
    PRINT("allocate 16 MiB after that", test_created(big));
    if (big >= 0)
        PRINT("free that 16 MiB block again", sceKernelFreeMemBlockForVM(big));

    test_exit();
    return 0;
}
