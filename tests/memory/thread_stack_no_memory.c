// Creating threads when their stack can't be had: the error, and whether main memory and the room
// for threads are what they were before the call. Main memory is filled with 1 MiB USER_RW blocks
// until one fails, then with 256 KiB, 64 KiB, 16 KiB and 4 KiB ones, each until one fails, so a
// 0x4000 stack can't fit. A stack of that size is asked for, then again after one 1 MiB block is
// freed, and sixteen threads with
// stacks of that size are kept at once after the frees. The room for threads isn't a count the
// process can read, so the sixteen stand in for it. The last cases ask for stacks of 0x10000000
// and 0x40000000, last of all, since an implementation that doesn't check the size may crash on
// them. For each, the create result, whether main memory is unchanged and whether sixteen threads
// still fit at once. A stack of 0x7FFFFFFF is in thread_stack_max_size, of its own. The cases:
// - a 0x4000 stack with main memory full
// - the same after freeing one 1 MiB block
// - sixteen threads with 0x4000 stacks made at once, after the blocks are freed
// - stacks of 0x10000000 and 0x40000000
// Main memory is measured to the page the way memblock_failed_alloc_free does. Every block and
// thread is freed before the next case, and the results of the full cases are printed once main
// memory is free, since writing the output file may need memory.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define PROBES 320
// The 1 MiB phase makes at most MIB_BLOCKS blocks, and each smaller phase at most STEP_BLOCKS
#define MAX_BLOCKS 520
#define MIB_BLOCKS 480
#define STEP_BLOCKS 8
#define STACK 0x4000
#define HELD 16

static SceUID blocks[MAX_BLOCKS];
static int used;
static SceUID probes[PROBES];

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

static int reported_main(void) {
    SceKernelFreeMemorySizeInfo sizes = { .size = sizeof(sizes) };
    sceKernelGetFreeMemorySize(&sizes);
    return sizes.size_user;
}

// The free size of main memory to the page, or -1 if it never drops
static int measure(void) {
    const int start = reported_main();
    int made = 0, now = start;
    while (made < PROBES && now == start) {
        const SceUID block
            = sceKernelAllocMemBlock("free probe", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
        if (block < 0)
            break;
        probes[made++] = block;
        now = reported_main();
    }
    for (int i = 0; i < made; i++)
        sceKernelFreeMemBlock(probes[i]);
    return now == start ? -1 : start + (made - 1) * (int)(4 * KIB);
}

// Keeps USER_RW blocks of the unit in blocks until one fails, or max are added, and returns the
// error, or 0 if max were added
static int fill(SceSize unit, int max) {
    const int end = used + max;
    while (used < end) {
        const SceUID block
            = sceKernelAllocMemBlock("fill", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, unit, NULL);
        if (block < 0)
            return block;
        blocks[used++] = block;
    }
    return 0;
}

static SceUID create(SceSize stack) {
    return sceKernelCreateThread("no memory", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, stack, 0, 0,
        NULL);
}

// Makes HELD threads with stacks of STACK at once, deletes them, and returns whether all were made
static int hold_threads(void) {
    SceUID threads[HELD];
    int made = 0;
    for (; made < HELD; made++) {
        threads[made] = create(STACK);
        if (threads[made] < 0)
            break;
    }
    for (int i = 0; i < made; i++)
        sceKernelDeleteThread(threads[i]);
    return made == HELD;
}

// Asks for a stack of the size, then prints the result, whether main memory is unchanged and
// whether the threads still fit
static void stack_case(const char *name, SceSize stack) {
    const int before = measure();
    const SceUID thread = create(stack);
    const int after = measure();
    if (thread >= 0)
        sceKernelDeleteThread(thread);
    PRINTF(test_created(thread), "%s: create", name);
    PRINTF(before == after, "%s: main free size unchanged", name);
    PRINTF(hold_threads(), "%s: 16 threads still fit at once", name);
}

int main(void) {
    // The first measure only warms up
    measure();

    // Main memory is full until the blocks are freed, so the results are printed after that
    const int full_mib = fill(MIB, MIB_BLOCKS);
    const int fit_mib = used >= 4;
    static const SceSize steps[] = { 256 * KIB, 64 * KIB, 16 * KIB, 4 * KIB };
    int full_steps[4];
    for (int i = 0; i < 4; i++)
        full_steps[i] = fill(steps[i], STEP_BLOCKS);
    // Pages the system gave back since the fill are taken again right before the create
    fill(4 * KIB, STEP_BLOCKS);
    const SceUID stack_full = create(STACK);
    if (stack_full >= 0)
        sceKernelDeleteThread(stack_full);
    const int freed_one = used > 0 ? sceKernelFreeMemBlock(blocks[0]) : -1;
    const SceUID stack_freed = create(STACK);
    if (stack_freed >= 0)
        sceKernelDeleteThread(stack_freed);
    const int held = hold_threads();
    for (int i = 1; i < used; i++)
        sceKernelFreeMemBlock(blocks[i]);

    PRINTF(full_mib, "1 MiB USER_RW blocks: the error when full");
    PRINTF(fit_mib, "1 MiB USER_RW blocks: at least 4 fit");
    PRINTF(full_steps[0], "256 KiB USER_RW blocks: the error when full");
    PRINTF(full_steps[1], "64 KiB USER_RW blocks: the error when full");
    PRINTF(full_steps[2], "16 KiB USER_RW blocks: the error when full");
    PRINTF(full_steps[3], "4 KiB USER_RW blocks: the error when full");
    PRINTF(test_created(stack_full), "a 0x4000 stack with main full: create");
    PRINTF(freed_one, "free one 1 MiB block: the result");
    PRINTF(test_created(stack_freed), "a 0x4000 stack after freeing one 1 MiB block: create");
    PRINTF(held, "16 threads with 0x4000 stacks at once after that: all made");

    stack_case("a stack of 0x10000000", 0x10000000);
    stack_case("a stack of 0x40000000", 0x40000000);

    test_exit();
    return 0;
}
