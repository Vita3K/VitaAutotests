// Creates given an option pointer that can't be read, alone and together with another bad
// argument, to see which error comes first. The option pointer is the base of a USER_RW block
// freed right before the call. Each case prints the create's result and deletes what a create
// made anyway. mutex_create_unmapped_options and unmapped_in already cover the option alone for a
// semaphore, a mutex, an event flag and a lightweight mutex. The cases:
// - a simple event create and a timer create with that option
// - a simple event, event flag, semaphore, mutex, timer and lightweight mutex create with that
//   option and a NULL name
// - the same creates with that option and the attribute 0x80000000
// - a lightweight mutex create with that option and a NULL work area, then a work area 4 bytes
//   past an 8 byte boundary
//
// On a PS Vita TV on firmware 3.65 every case fails with 0x80022005: each create reads its option
// before the name, the attribute and the lightweight mutex's work area.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define BAD_ATTR 0x80000000

static SceKernelLwMutexWork works[2] __attribute__((aligned(8)));

// Allocates a block and frees it, and returns its base, which is in no block until the next
// allocation
static void *unmapped(void) {
    const SceUID block
        = sceKernelAllocMemBlock("option unreadable", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x10000,
            NULL);
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    sceKernelFreeMemBlock(block);
    return base;
}

static void print_create(const char *kind, const char *where, SceUID id, int (*end)(SceUID)) {
    PRINTF(test_created(id), "%s: %s", kind, where);
    if (id >= 0)
        end(id);
}

static void print_lw_create(const char *where, SceKernelLwMutexWork *work, int result) {
    PRINTF(test_created(result), "lightweight mutex: %s", where);
    if (result >= 0)
        sceKernelDeleteLwMutex(work);
}

// Runs every create with the option there, the given name and the given attribute
static void run(const char *where, const char *name, SceUInt32 attr) {
    print_create("simple event", where, sceKernelCreateSimpleEvent(name, attr, 0, unmapped()),
        sceKernelDeleteSimpleEvent);
    print_create("event flag", where, sceKernelCreateEventFlag(name, attr, 0, unmapped()),
        sceKernelDeleteEventFlag);
    print_create("semaphore", where, sceKernelCreateSema(name, attr, 0, 1, unmapped()),
        sceKernelDeleteSema);
    print_create("mutex", where, sceKernelCreateMutex(name, attr, 0, unmapped()),
        sceKernelDeleteMutex);
    print_create("timer", where, sceKernelCreateTimer(name, attr, unmapped()),
        sceKernelDeleteTimer);
    print_lw_create(where, &works[0], sceKernelCreateLwMutex(&works[0], name, attr, 0, unmapped()));
}

int main(void) {
    print_create("simple event", "the option alone",
        sceKernelCreateSimpleEvent("option unreadable", 0, 0, unmapped()),
        sceKernelDeleteSimpleEvent);
    print_create("timer", "the option alone",
        sceKernelCreateTimer("option unreadable", 0, unmapped()), sceKernelDeleteTimer);

    run("the option and a NULL name", NULL, 0);
    run("the option and the attribute 0x80000000", "option unreadable", BAD_ATTR);

    print_lw_create("the option and a NULL work area", NULL,
        sceKernelCreateLwMutex(NULL, "option unreadable", 0, 0, unmapped()));
    SceKernelLwMutexWork *misaligned = (SceKernelLwMutexWork *)((char *)&works[0] + 4);
    print_lw_create("the option and a misaligned work area", misaligned,
        sceKernelCreateLwMutex(misaligned, "option unreadable", 0, 0, unmapped()));

    test_exit();
    return 0;
}
