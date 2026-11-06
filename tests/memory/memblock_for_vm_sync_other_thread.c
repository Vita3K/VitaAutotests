// Code that one thread rewrites in a memory block for code and syncs with sceKernelSyncVMDomain,
// while another thread ran the old code at the same address before. The other thread runs the
// function, then the main thread writes a new function there with the domain open, closes it and
// syncs the range, and then the other thread runs the same address again. The main thread never
// runs the code itself. Prints what each run returned.
//
// On a PS Vita TV on firmware 3.65 the second run returns 2, so the other thread runs the new code.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define MIB 0x100000

// vitasdk has a stub for it but no prototype
int sceKernelFreeMemBlockForVM(SceUID uid);

// ARM code for functions that return 1 and 2: mov r0, #1 or #2, and bx lr
static const SceUInt32 returns_1[] = { 0xE3A00001, 0xE12FFF1E };
static const SceUInt32 returns_2[] = { 0xE3A00002, 0xE12FFF1E };

static SceUID ran_once, rewritten;
static int (*function)(void);
static int first_run, second_run, rewrite_wait;

// Runs the function, waits until the main thread rewrote it, then runs it again
static int runner(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    first_run = function();
    sceKernelSignalSema(ran_once, 1);
    SceUInt timeout = TEST_WAIT_US;
    rewrite_wait = sceKernelWaitSema(rewritten, 1, &timeout);
    if (rewrite_wait == 0)
        second_run = function();
    return 0;
}

// Writes code into the block with the domain open, then closes it and syncs the range
static int write_code(SceUID block, void *base, const SceUInt32 *code) {
    sceKernelOpenVMDomain();
    volatile SceUInt32 *words = base;
    words[0] = code[0];
    words[1] = code[1];
    sceKernelCloseVMDomain();
    return sceKernelSyncVMDomain(block, base, 2 * sizeof(SceUInt32));
}

int main(void) {
    const SceUID block = sceKernelAllocMemBlockForVM("vm sync", MIB);
    PRINT("allocate a block for code", test_created(block));
    if (block < 0) {
        test_exit();
        return 0;
    }
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    function = (int (*)(void))base;
    ran_once = sceKernelCreateSema("ran once", 0, 0, 1, NULL);
    rewritten = sceKernelCreateSema("rewritten", 0, 0, 1, NULL);
    first_run = second_run = rewrite_wait = -1;

    PRINT("write and sync the first function", write_code(block, base, returns_1));
    const SceUID thread = test_start_thread(runner);
    SceUInt timeout = TEST_WAIT_US;
    PRINT("the other thread ran it", sceKernelWaitSema(ran_once, 1, &timeout));
    PRINT("write and sync the second function", write_code(block, base, returns_2));
    sceKernelSignalSema(rewritten, 1);
    test_join_thread(thread);
    PRINT("the other thread waited for the rewrite", rewrite_wait);
    PRINT("the first run returned", first_run);
    PRINT("the second run returned", second_run);

    sceKernelDeleteSema(ran_once);
    sceKernelDeleteSema(rewritten);
    PRINT("free the block", sceKernelFreeMemBlockForVM(block));
    test_exit();
    return 0;
}
