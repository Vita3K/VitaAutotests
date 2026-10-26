// Lightweight mutex calls given work areas that are misaligned, read only or NULL, and the order
// the create checks report their errors in. The read only area is a 64 KiB USER_RW block with
// ENABLE_OPEN, opened with flags 0x10, and the writable view of the same block holds what the read
// only view shows. The app never locks, unlocks or reads the info of a work area in read only or
// misaligned memory, and misaligned areas only go to create. The unmapped areas are in
// lw_work_area_unmapped and lw_work_area_freed, since an implementation may crash on them. The
// cases:
// - a create with a work area misaligned by 1, 2 and 4 bytes, with the other arguments good, then
//   a create misaligned by 4 with a bad attribute
// - the create checks on a good work area: a bad attribute with a bad count, a bad attribute with
//   an option struct of size 8, a count of -1 with an option struct of size 8, and option structs
//   of size 0, 4 and 5
// - a create into the read only area, whether that area is unchanged, then a create in a good area
// - a mutex made in the writable view and deleted through the read only view, then its info by
//   UID, and a second delete through the writable view
// - last, a NULL work area, NULL names and a delete of NULL, in case an implementation reads them
// Each mutex a create makes is deleted.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define KIB 0x400
#define ENABLE_OPEN 0x4000
#define READ_ONLY 0x10
#define FILL 0xEE
#define RO_OFFSET 0x1000
#define LIVE_OFFSET 0x2000
#define POOL_OFFSET 0x20

// The option struct games pass to the allocation, which asks for ENABLE_OPEN
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

// An option struct of 8 bytes, whose size field the create calls read
typedef struct OptParam8 {
    SceSize size;
    SceUInt32 reserved;
} OptParam8;

// Work areas that are misaligned by offset minus POOL_OFFSET, in a pool aligned to 8 bytes
static unsigned char pool[0x60] __attribute__((aligned(8)));
static SceKernelLwMutexWork good;

// Sets size bytes at area to the fill value, one byte at a time
static void fill(unsigned char *area, unsigned int size) {
    for (unsigned int i = 0; i < size; i++)
        area[i] = FILL;
}

// Whether size bytes at area still hold the fill value, read one byte at a time
static int still_filled(const unsigned char *area, unsigned int size) {
    int same = 1;
    for (unsigned int i = 0; i < size; i++)
        same &= area[i] == FILL;
    return same;
}

// The work area at offset from base, which may be misaligned
static SceKernelLwMutexWork *work_at(unsigned char *base, unsigned int offset) {
    return (SceKernelLwMutexWork *)(void *)(base + offset);
}

// The base address of a memory block
static unsigned char *base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return base;
}

// Creates a lightweight mutex named m in work with the attribute, count and option struct, deletes
// it if the create worked, and returns the result of the create
static int create_and_delete(SceKernelLwMutexWork *work, unsigned int attr, int count,
    const SceKernelLwMutexOptParam *opt) {
    const int result = sceKernelCreateLwMutex(work, "m", attr, count, opt);
    if (result == 0)
        sceKernelDeleteLwMutex(work);
    return result;
}

int main(void) {
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    const SceUID original = sceKernelAllocMemBlock("lw in", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
        64 * KIB, (SceKernelAllocMemBlockOpt *)&opt);
    PRINT("allocate the original", test_created(original));
    const SceUID opened = original >= 0 ? sceKernelOpenMemBlock("lw in", READ_ONLY) : original;
    PRINT("open it read only", test_created(opened));
    if (opened < 0) {
        if (original >= 0)
            sceKernelFreeMemBlock(original);
        test_exit();
        return 0;
    }
    unsigned char *writable = base_of(original);
    unsigned char *readable = base_of(opened);

    // Misaligned work areas, with the other arguments good
    const int misalign[] = { 1, 2, 4 };
    for (unsigned int i = 0; i < sizeof(misalign) / sizeof(misalign[0]); i++) {
        fill(pool, sizeof(pool));
        const unsigned int offset = POOL_OFFSET + misalign[i];
        PRINTF(create_and_delete(work_at(pool, offset), 0, 0, NULL),
            "work area misaligned by %d: create", misalign[i]);
        PRINTF(still_filled(pool + offset, sizeof(good)),
            "work area misaligned by %d: area unchanged", misalign[i]);
    }
    fill(pool, sizeof(pool));
    PRINT("work area misaligned by 4, bad attribute: create",
        create_and_delete(work_at(pool, POOL_OFFSET + 4), 0x80000000, 0, NULL));

    // The create checks on a good work area
    OptParam8 opt8 = { .size = 8 };
    const SceKernelLwMutexOptParam *opt8_ptr = (const SceKernelLwMutexOptParam *)&opt8;
    PRINT("bad attribute and a bad count", create_and_delete(&good, 0x80000000, -1, NULL));
    PRINT("bad attribute and an option struct of size 8",
        create_and_delete(&good, 0x80000000, 0, opt8_ptr));
    PRINT("an option struct of size 8 and a count of -1",
        create_and_delete(&good, 0, -1, opt8_ptr));
    const SceKernelLwMutexOptParam size0 = { .size = 0 };
    const SceKernelLwMutexOptParam size4 = { .size = 4 };
    OptParam8 size5 = { .size = 5 };
    PRINT("an option struct of size 0", create_and_delete(&good, 0, 0, &size0));
    PRINT("an option struct of size 4", create_and_delete(&good, 0, 0, &size4));
    PRINT("an option struct of size 5",
        create_and_delete(&good, 0, 0, (const SceKernelLwMutexOptParam *)&size5));

    // A create into the read only view, whose area the create can't write
    fill(writable + RO_OFFSET, sizeof(good));
    PRINT("read only work area: create",
        create_and_delete(work_at(readable, RO_OFFSET), 0, 0, NULL));
    PRINT("read only work area: the area is unchanged",
        still_filled(writable + RO_OFFSET, sizeof(good)));
    PRINT("read only work area: a create in a good area afterwards",
        create_and_delete(&good, 0, 0, NULL));

    // A mutex made in the writable view and deleted through the read only view
    SceKernelLwMutexWork *live = work_at(writable, LIVE_OFFSET);
    PRINT("live mutex in writable memory: create", sceKernelCreateLwMutex(live, "m", 0, 0, NULL));
    LwMutexInfo info = { .size = sizeof(info) };
    PRINT("live mutex in writable memory: info by its work area",
        sceKernelGetLwMutexInfo(live, &info));
    PRINT("delete it through the read only view",
        sceKernelDeleteLwMutex(work_at(readable, LIVE_OFFSET)));
    LwMutexInfo gone = { .size = sizeof(gone) };
    PRINT("info by UID after that delete", sceKernelGetLwMutexInfoById(info.uid, &gone));
    PRINT("delete it again through the writable view", sceKernelDeleteLwMutex(live));

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("NULL work area with a bad attribute and count",
        sceKernelCreateLwMutex(NULL, "m", 0x80000000, -1, NULL));
    PRINT("NULL work area", sceKernelCreateLwMutex(NULL, "m", 0, 0, NULL));
    PRINT("NULL name with a bad count", sceKernelCreateLwMutex(&good, NULL, 0, -1, NULL));
    PRINT("NULL name", sceKernelCreateLwMutex(&good, NULL, 0, 0, NULL));
    PRINT("misaligned by 4, NULL name",
        sceKernelCreateLwMutex(work_at(pool, POOL_OFFSET + 4), NULL, 0, 0, NULL));
    PRINT("delete NULL", sceKernelDeleteLwMutex(NULL));

    sceKernelCloseMemBlock(opened);
    sceKernelFreeMemBlock(original);
    test_exit();
    return 0;
}
