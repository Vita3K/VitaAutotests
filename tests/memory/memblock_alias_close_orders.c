// Three aliases of one 1 MiB block with ENABLE_OPEN, and the original and the aliases ended in six
// orders. The original is E. The aliases are A and C, opened with 0x30, and B, opened with 0x10.
// Each order makes the original and the three aliases, then ends them one step at a time, the
// first step with close, the second with the usual free, and so on. After each step it prints:
// - the step's result
// - the find by address of the original and of each alias, ended or not
// - the base by UID of each alias that is left
// - whether each alias that is left reads the word the original holds. The test writes a new
//   value through the original while it lives, then reads through each alias left, only while
//   the find by address gives that alias.
// - the main memory taken since the order began, in KiB
//
// A find prints 0 when it gives the block, the error when it fails and 1 when it gives another
// UID. The orders are E A B C, A B C E, C E B A, B C A E, A E C B and E C B A.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define ENABLE_OPEN 0x4000

#define OBJECTS 4
#define STEPS 4

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

// Index 0 is the original, and 1 to 3 are the aliases
static const char *const labels[OBJECTS] = { "E", "A", "B", "C" };
static const SceUInt32 flags[OBJECTS] = { 0, 0x30, 0x10, 0x30 };

typedef struct Order {
    const char *name;
    int steps[STEPS];
} Order;

static const Order orders[] = {
    { "E A B C", { 0, 1, 2, 3 } },
    { "A B C E", { 1, 2, 3, 0 } },
    { "C E B A", { 3, 0, 2, 1 } },
    { "B C A E", { 2, 3, 1, 0 } },
    { "A E C B", { 1, 0, 3, 2 } },
    { "E C B A", { 0, 3, 2, 1 } },
};

// The last value the test wrote through the original
static SceUInt32 mark;

static char *base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return base;
}

static int find_result(const char *base, SceUID block) {
    const SceUID found = sceKernelFindMemBlockByAddr(base, 0);
    return found == block ? 0 : found < 0 ? found : 1;
}

// Whether the block at base is still found and holds value, read only if it's found
static int reads(const char *base, SceUID block, SceUInt32 value) {
    return find_result(base, block) == 0 && *(volatile SceUInt32 *)base == value;
}

// Writes value at base if the block is still found there
static void write_found(const char *base, SceUID block, SceUInt32 value) {
    if (find_result(base, block) == 0)
        *(volatile SceUInt32 *)base = value;
}

static int main_taken(const SceKernelFreeMemorySizeInfo *before) {
    SceKernelFreeMemorySizeInfo now = { .size = sizeof(now) };
    sceKernelGetFreeMemorySize(&now);
    return (before->size_user - now.size_user) / KIB;
}

// Makes the original and the aliases of one order, then ends them step by step and prints what
// each step leaves
static void run_order(int number, const Order *order) {
    SceKernelFreeMemorySizeInfo before = { .size = sizeof(before) };
    sceKernelGetFreeMemorySize(&before);
    char name[32];
    sceClibSnprintf(name, sizeof(name), "close orders %d", number);

    SceUID uid[OBJECTS] = { -1, -1, -1, -1 };
    char *base[OBJECTS] = { NULL, NULL, NULL, NULL };
    int ended[OBJECTS] = { 0, 0, 0, 0 };
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    uid[0] = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, MIB,
        (SceKernelAllocMemBlockOpt *)&opt);
    PRINTF(test_created(uid[0]), "%s: allocate the original", order->name);
    if (uid[0] < 0)
        return;
    base[0] = base_of(uid[0]);
    mark++;
    write_found(base[0], uid[0], mark);

    for (int i = 1; i < OBJECTS; i++) {
        uid[i] = sceKernelOpenMemBlock(name, flags[i]);
        PRINTF(test_created(uid[i]), "%s: open %s with 0x%X", order->name, labels[i],
            (unsigned)flags[i]);
        if (uid[i] < 0)
            ended[i] = 1;
        else
            base[i] = base_of(uid[i]);
    }

    for (int step = 0; step < STEPS; step++) {
        const int i = order->steps[step];
        const int close = step % 2 == 0;
        if (!ended[i]) {
            const int result = close ? sceKernelCloseMemBlock(uid[i])
                                     : sceKernelFreeMemBlock(uid[i]);
            ended[i] = 1;
            PRINTF(result, "%s: step %d, %s %s", order->name, step + 1, close ? "close" : "free",
                labels[i]);
        }
        if (!ended[0]) {
            mark++;
            write_found(base[0], uid[0], mark);
        }

        for (int j = 0; j < OBJECTS; j++)
            if (uid[j] >= 0)
                PRINTF(find_result(base[j], uid[j]), "%s: step %d, find %s", order->name,
                    step + 1, labels[j]);
        for (int j = 1; j < OBJECTS; j++) {
            void *out = NULL;
            if (uid[j] >= 0 && !ended[j])
                PRINTF(sceKernelGetMemBlockBase(uid[j], &out), "%s: step %d, base of %s by UID",
                    order->name, step + 1, labels[j]);
        }
        for (int j = 1; j < OBJECTS; j++)
            if (uid[j] >= 0 && !ended[j])
                PRINTF(reads(base[j], uid[j], mark), "%s: step %d, %s reads the original's word",
                    order->name, step + 1, labels[j]);
        PRINTF(main_taken(&before), "%s: step %d, KiB of main memory taken", order->name, step + 1);
    }
}

int main(void) {
    for (unsigned int i = 0; i < sizeof(orders) / sizeof(orders[0]); i++)
        run_order(i + 1, &orders[i]);
    test_exit();
    return 0;
}
