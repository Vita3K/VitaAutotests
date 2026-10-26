// Whether memory blocks and their aliases count toward a process's object limits. Semaphores are
// created, and openable semaphores opened, until one fails, first with nothing else live and then
// with blocks or aliases live. The cases:
// - plain semaphores created until one fails: with nothing live, then with 256 plain 4 KiB USER_RW
//   blocks live. Whether the count drops by 0 and whether it drops by 256 while they are live, and
//   whether the count is back after they are freed
// - the same with openable semaphores, against 64 4 KiB USER_RW blocks made with ENABLE_OPEN
// - opens of one openable semaphore until one fails (0x8002450C), then with 128 aliases of an
//   ENABLE_OPEN block live, opened with flags 0x30. The same comparisons as above
//
// Only differences between the counts are printed, since the count depends on what the process
// holds, plus the errors that stopped each loop. The test keeps at most 256 blocks and 128 aliases
// live at once, and frees everything it made before the next case.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define ENABLE_OPEN 0x4000
#define LIMIT 4096
#define PLAIN_BLOCKS 256
#define NAMED_BLOCKS 64
#define ALIASES 128

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

static SceUID objects[LIMIT];

// Creates semaphores into objects until one fails or the limit is reached, and returns how many
// were made. The error of the one that failed is stored in error, or 0 at the limit.
static int create_semas(SceInt32 *error) {
    int count = 0;
    *error = 0;
    while (count < LIMIT) {
        const SceUID id = sceKernelCreateSema("limit", 0, 0, 1, NULL);
        if (id < 0) {
            *error = id;
            break;
        }
        objects[count++] = id;
    }
    return count;
}

// Creates openable semaphores into objects, each under a name of its own, until one fails
static int create_openable(SceInt32 *error) {
    int count = 0;
    *error = 0;
    while (count < LIMIT) {
        char name[32];
        sceClibSnprintf(name, sizeof(name), "open limit %d", count);
        const SceUID id = sceKernelCreateSema(name, SCE_KERNEL_ATTR_OPENABLE, 0, 1, NULL);
        if (id < 0) {
            *error = id;
            break;
        }
        objects[count++] = id;
    }
    return count;
}

static void delete_semas(int count) {
    for (int i = 0; i < count; i++)
        sceKernelDeleteSema(objects[i]);
}

static SceUID plain_blocks[PLAIN_BLOCKS];

static void plain_semas(void) {
    SceInt32 error = 0;
    const int without = create_semas(&error);
    const SceInt32 without_error = error;
    delete_semas(without);

    int made = 0;
    for (int i = 0; i < PLAIN_BLOCKS; i++) {
        plain_blocks[i] = sceKernelAllocMemBlock("limit block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
        made += plain_blocks[i] >= 0;
    }
    const int with = create_semas(&error);
    const SceInt32 with_error = error;
    delete_semas(with);
    int freed = 0;
    for (int i = 0; i < PLAIN_BLOCKS; i++)
        freed += plain_blocks[i] >= 0 && sceKernelFreeMemBlock(plain_blocks[i]) == 0;

    const int after = create_semas(&error);
    delete_semas(after);

    PRINT("plain semaphores: the error with nothing live", without_error);
    PRINT("plain semaphores: the error with the blocks live", with_error);
    PRINT("plain blocks: all 256 are made", made == PLAIN_BLOCKS);
    PRINT("plain blocks: every free works", freed == made);
    PRINT("plain blocks: the count drops by 0 while they are live", without - with == 0);
    PRINT("plain blocks: the count drops by 256 while they are live", without - with == PLAIN_BLOCKS);
    PRINT("plain blocks: the count is back after they are freed", after == without);
}

static SceUID named_blocks[NAMED_BLOCKS];

static void named_semas(void) {
    SceInt32 error = 0;
    const int without = create_openable(&error);
    const SceInt32 without_error = error;
    delete_semas(without);

    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    int made = 0;
    for (int i = 0; i < NAMED_BLOCKS; i++) {
        char name[32];
        sceClibSnprintf(name, sizeof(name), "limit block %d", i);
        named_blocks[i] = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB,
                                                 (SceKernelAllocMemBlockOpt *)&opt);
        made += named_blocks[i] >= 0;
    }
    const int with = create_openable(&error);
    const SceInt32 with_error = error;
    delete_semas(with);
    int freed = 0;
    for (int i = 0; i < NAMED_BLOCKS; i++)
        freed += named_blocks[i] >= 0 && sceKernelFreeMemBlock(named_blocks[i]) == 0;

    const int after = create_openable(&error);
    delete_semas(after);

    PRINT("openable semaphores: the error with nothing live", without_error);
    PRINT("openable semaphores: the error with the blocks live", with_error);
    PRINT("ENABLE_OPEN blocks: all 64 are made", made == NAMED_BLOCKS);
    PRINT("ENABLE_OPEN blocks: every free works", freed == made);
    PRINT("ENABLE_OPEN blocks: the count drops by 0 while they are live", without - with == 0);
    PRINT("ENABLE_OPEN blocks: the count drops by 64 while they are live", without - with == NAMED_BLOCKS);
    PRINT("ENABLE_OPEN blocks: the count is back after they are freed", after == without);
}

static SceUID opened[LIMIT];

// Opens the semaphore by name into opened until one open fails or the limit is reached, closes them
// all, and returns how many opens worked. The error of the one that failed is stored in error.
static int count_opens(const char *name, SceInt32 *error) {
    int count = 0;
    *error = 0;
    while (count < LIMIT) {
        const SceUID id = sceKernelOpenSema(name);
        if (id < 0) {
            *error = id;
            break;
        }
        opened[count++] = id;
    }
    for (int i = 0; i < count; i++)
        sceKernelCloseSema(opened[i]);
    return count;
}

static SceUID aliases[ALIASES];

static void alias_opens(void) {
    const SceUID target = sceKernelCreateSema("open target", SCE_KERNEL_ATTR_OPENABLE, 0, 1, NULL);
    SceInt32 error = 0;
    const int without = count_opens("open target", &error);
    const SceInt32 without_error = error;

    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    const SceUID original = sceKernelAllocMemBlock("alias limit", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB,
                                                   (SceKernelAllocMemBlockOpt *)&opt);
    int made = 0;
    for (int i = 0; i < ALIASES && original >= 0; i++) {
        aliases[i] = sceKernelOpenMemBlock("alias limit", 0x30);
        made += aliases[i] >= 0;
    }
    const int with = count_opens("open target", &error);
    const SceInt32 with_error = error;
    int closed = 0;
    for (int i = 0; i < made; i++)
        closed += sceKernelCloseMemBlock(aliases[i]) == 0;
    if (original >= 0)
        sceKernelFreeMemBlock(original);

    const int after = count_opens("open target", &error);
    sceKernelDeleteSema(target);

    PRINT("openable opens: the error with nothing live", without_error);
    PRINT("openable opens: the error with the aliases live", with_error);
    PRINT("aliases: the original is made", original >= 0);
    PRINT("aliases: all 128 open", made == ALIASES);
    PRINT("aliases: every close works", closed == made);
    PRINT("aliases: the opens drop by 0 while they are live", without - with == 0);
    PRINT("aliases: the opens drop by 128 while they are live", without - with == ALIASES);
    PRINT("aliases: the opens are back after they are closed", after == without);
}

int main(void) {
    // A line first, so the output file is open before the creates fill anything up
    const SceUID first = sceKernelCreateSema("first", 0, 0, 1, NULL);
    PRINT("create a semaphore before the others", test_created(first));
    sceKernelDeleteSema(first);

    plain_semas();
    named_semas();
    alias_opens();
    test_exit();
    return 0;
}
