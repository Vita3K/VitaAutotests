// The free sizes an app sees, from sceKernelGetFreeMemorySize, next to the memory it didn't
// allocate itself. The main and CDRAM free sizes move in steps of 1 MiB, so the test finds them
// to the unit of a block: it makes 4 KiB USER_RW blocks, or 256 KiB USER_CDRAM_RW ones, until the
// reported size drops, and frees them again. Then the size is the reported one plus a unit for
// each block before the one it dropped at. A first measure of each, which only warms up, isn't
// printed. The cases:
// - whether measuring the main free size twice in a row gives the same size, and how much a
//   1 MiB and a 64 KiB USER_RW block take from it as measured, and whether freeing gives it back
// - the CDRAM and physically contiguous free sizes at the start, as reported, and the CDRAM one
//   as measured
// - whether sceKernelGetMemBlockInfoByAddr finds the blocks behind the app's code, data, heap,
//   main thread stack and main thread TLS. Their sizes change with the build, so they aren't
//   printed.
// - how much main memory and CDRAM, as measured, and physically contiguous memory, as reported,
//   loading the fiber system module takes, and whether unloading it gives it back
// - the same for sceGxmInitialize and sceGxmTerminate
// The main memory these two take is printed as a range, since it differs between runs and between
// consoles.
// Everything is measured first and printed after, so the output file the test writes doesn't
// take memory in between.
//
// On a PS Vita TV on firmware 3.65 the measure is exact to the page: twice in a row it gives the
// same size, and a 1 MiB and a 64 KiB block take exactly their size. CDRAM starts with 112 MiB
// free, the same as measured, and physically contiguous memory with 26 MiB. Loading the fiber
// module takes between 64 and 128 KiB of main memory, 84 KiB in five of six runs and 80 KiB in the
// other, and nothing else. Initializing libgxm takes 2 MiB and 12 KiB of main memory and 16 MiB of
// CDRAM. Unloading and terminating give all of it back.
#include <stdlib.h>

#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define PROBES 320

enum Kind { MAIN, CDRAM };

typedef struct Sizes {
    int main;
    int cdram;
    int phycont;
} Sizes;

static SceUID probes[PROBES];
static volatile int data_word = 1;

static SceKernelFreeMemorySizeInfo reported(void) {
    SceKernelFreeMemorySizeInfo sizes = { .size = sizeof(sizes) };
    sceKernelGetFreeMemorySize(&sizes);
    return sizes;
}

static int reported_kind(enum Kind kind) {
    const SceKernelFreeMemorySizeInfo sizes = reported();
    return kind == MAIN ? sizes.size_user : sizes.size_cdram;
}

// The free size of the kind to the unit of its blocks, or -1 if it never drops
static int measure(enum Kind kind) {
    const SceUInt32 type
        = kind == MAIN ? SCE_KERNEL_MEMBLOCK_TYPE_USER_RW : SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW;
    const SceSize unit = kind == MAIN ? 4 * KIB : 256 * KIB;
    const int start = reported_kind(kind);
    int made = 0, now = start;
    while (made < PROBES && now == start) {
        const SceUID block = sceKernelAllocMemBlock("free probe", type, unit, NULL);
        if (block < 0)
            break;
        probes[made++] = block;
        now = reported_kind(kind);
    }
    for (int i = 0; i < made; i++)
        sceKernelFreeMemBlock(probes[i]);
    return now == start ? -1 : start + (made - 1) * (int)unit;
}

static Sizes measure_all(void) {
    const Sizes sizes = { measure(MAIN), measure(CDRAM), reported().size_phycont };
    return sizes;
}

// The main memory a USER_RW block of the size takes as measured, and in *back whether freeing it
// gives that back
static int block_takes(SceSize size, int *back) {
    const int before = measure(MAIN);
    const SceUID block
        = sceKernelAllocMemBlock("free taken", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, NULL);
    const int with = measure(MAIN);
    sceKernelFreeMemBlock(block);
    *back = measure(MAIN) == before;
    return block < 0 ? block : before - with;
}

// The size of the block behind addr, or 0 if the info call fails
static int block_size(const void *addr) {
    SceKernelMemBlockInfo info = { .size = sizeof(info) };
    return sceKernelGetMemBlockInfoByAddr((void *)addr, &info) < 0 ? 0 : (int)info.mappedSize;
}

static void display_callback(const void *data) {
    (void)data;
}

static int initialize_gxm(void) {
    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = 2;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = 4;
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    return sceGxmInitialize(&init);
}

// Prints what loading or initializing took. The main memory is printed as a range between above
// and below KiB, since the fiber module's varies by a page between runs and libgxm's between
// consoles.
static void print_taken(const char *name, const Sizes *before, const Sizes *with, int above, int below) {
    const int main = before->main - with->main;
    PRINTF(main > above * KIB, "%s: it takes more than %d KiB of main memory", name, above);
    PRINTF(main < below * KIB, "%s: it takes less than %d KiB of main memory", name, below);
    PRINTF(before->cdram - with->cdram, "%s: the CDRAM it takes", name);
    PRINTF(before->phycont - with->phycont, "%s: the physically contiguous memory it takes", name);
}

static void print_back(const char *name, const Sizes *before, const Sizes *after) {
    PRINTF(before->main == after->main && before->cdram == after->cdram
            && before->phycont == after->phycont,
        "%s: the free sizes are back", name);
}

int main(void) {
    measure(MAIN);
    measure(CDRAM);
    const int first = measure(MAIN), second = measure(MAIN);
    int mib_back, small_back;
    const int mib_taken = block_takes(MIB, &mib_back);
    const int small_taken = block_takes(64 * KIB, &small_back);

    const SceKernelFreeMemorySizeInfo start = reported();
    const int cdram_start = measure(CDRAM);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    void *heap = malloc(16);
    const int code = block_size((const void *)((SceUIntPtr)main & ~(SceUIntPtr)1));
    const int data = block_size((const void *)&data_word);
    const int heap_size = block_size(heap);
    const int stack = block_size(info.stack);
    const int tls = block_size(sceKernelGetTLSAddr(0));
    // Measured as in the hardware runs, so the steps after it start from the same state
    measure(MAIN);

    const Sizes before_fiber = measure_all();
    const int loaded = sceSysmoduleLoadModule(SCE_SYSMODULE_FIBER);
    const Sizes with_fiber = measure_all();
    const int unloaded = loaded == 0 ? sceSysmoduleUnloadModule(SCE_SYSMODULE_FIBER) : loaded;
    const Sizes after_fiber = measure_all();

    const Sizes before_gxm = measure_all();
    const int initialized = initialize_gxm();
    const Sizes with_gxm = measure_all();
    const int terminated = initialized == 0 ? sceGxmTerminate() : initialized;
    const Sizes after_gxm = measure_all();

    PRINT("main: measuring twice in a row gives the same size", first >= 0 && first == second);
    PRINT("main: what a 1 MiB block takes", mib_taken);
    PRINT("main: freeing it gives that back", mib_back);
    PRINT("main: what a 64 KiB block takes", small_taken);
    PRINT("main: freeing it gives that back", small_back);

    PRINT("CDRAM at the start, as reported", start.size_cdram);
    PRINT("CDRAM at the start, as measured", cdram_start);
    PRINT("physically contiguous memory at the start", start.size_phycont);
    PRINT("main: the blocks of the app's code, data, heap, stack and TLS are found",
        code && data && heap_size && stack && tls);

    PRINT("load the fiber module", loaded);
    print_taken("the fiber module", &before_fiber, &with_fiber, 64, 128);
    PRINT("unload it", unloaded);
    print_back("after the unload", &before_fiber, &after_fiber);

    PRINT("initialize libgxm", initialized);
    print_taken("libgxm", &before_gxm, &with_gxm, 2048, 2304);
    PRINT("terminate it", terminated);
    print_back("after terminating it", &before_gxm, &after_gxm);

    free(heap);
    test_exit();
    return 0;
}
