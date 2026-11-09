// Which error sceKernelCreateCallback gives when more than one argument is wrong, which shows the
// order of its checks. The wrong arguments are a name of 32 or more characters with attribute bit
// 0x80, a nonzero attribute, a function at a data address, a NULL name and a name at an address no
// memory block maps. The unmapped name is the base of a 64 KiB USER_RW block freed right before the
// call, as in unmapped_in, and the test checks first with sceKernelFindMemBlockByAddr that it is in
// no block. Every create that works is deleted at once, and a callback made with a 32 character
// name and attribute 0 shows the length of the name it kept. The results are printed at the end.
// The NULL and unmapped names come last.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define KIB 0x400
#define LONGEST 255

static char name_31[32], name_32[33], name_255[LONGEST + 1];
// Only its address is used, as a function that isn't code
static int data_word;

static int callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    return 0;
}

// Creates a callback, deletes it if it was made, and returns 0 or the error
static int create(const char *name, SceUInt32 attr, void *function) {
    const SceUID id = sceKernelCreateCallback(name, attr, (SceKernelCallbackFunction)function, NULL);
    if (id >= 0)
        sceKernelDeleteCallback(id);
    return test_created(id);
}

// Allocates a block and frees it, and returns its base, which is in no block until the next
// allocation. in_no_block is set to whether sceKernelFindMemBlockByAddr finds no block there.
static char *unmapped(int *in_no_block) {
    const SceUID block = sceKernelAllocMemBlock("unmapped name", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 64 * KIB, NULL);
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    sceKernelFreeMemBlock(block);
    *in_no_block = sceKernelFindMemBlockByAddr(base, 0) < 0;
    return base;
}

typedef struct Case {
    const char *label;
    const char *name;
    SceUInt32 attr;
    void *function;
} Case;

int main(void) {
    sceClibMemset(name_31, 'a', 31);
    sceClibMemset(name_32, 'a', 32);
    sceClibMemset(name_255, 'a', LONGEST);

    void *const code = (void *)callback;
    void *const data = (void *)&data_word;
    const Case cases[] = {
        { "a 32 character name with attr 0x80", name_32, 0x80, code },
        { "a 255 character name with attr 0x80", name_255, 0x80, code },
        { "a 31 character name with attr 0x80", name_31, 0x80, code },
        { "a 32 character name with attr 0x81", name_32, 0x81, code },
        { "a 32 character name with attr 0x1", name_32, 0x1, code },
        { "a 32 character name, attr 0x80 and a function at a data address", name_32, 0x80, data },
        { "a 31 character name, attr 0x80 and a function at a data address", name_31, 0x80, data },
        { "a 31 character name, attr 0 and a function at a data address", name_31, 0, data },
    };
    int results[sizeof(cases) / sizeof(cases[0])];
    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        results[i] = create(cases[i].name, cases[i].attr, cases[i].function);

    const SceUID kept = sceKernelCreateCallback(name_32, 0, callback, NULL);
    CallbackInfo info = { .size = sizeof(info) };
    sceKernelGetCallbackInfo(kept, (SceKernelCallbackInfo *)&info);
    const int kept_length = sceClibStrnlen(info.name, sizeof(info.name));
    if (kept >= 0)
        sceKernelDeleteCallback(kept);

    const int null_0x80 = create(NULL, 0x80, code);
    const int null_0x1 = create(NULL, 0x1, code);
    int no_block_0x80 = 0, no_block_0x1 = 0;
    const int unmapped_0x80 = create(unmapped(&no_block_0x80), 0x80, code);
    const int unmapped_0x1 = create(unmapped(&no_block_0x1), 0x1, code);

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        PRINT(cases[i].label, results[i]);
    PRINT("a 32 character name with attr 0: created", test_created(kept));
    PRINT("its info name length", kept_length);
    PRINT("a NULL name with attr 0x80", null_0x80);
    PRINT("a NULL name with attr 0x1", null_0x1);
    PRINT("the unmapped name is in no block, for attr 0x80", no_block_0x80);
    PRINT("an unmapped name with attr 0x80", unmapped_0x80);
    PRINT("the unmapped name is in no block, for attr 0x1", no_block_0x1);
    PRINT("an unmapped name with attr 0x1", unmapped_0x1);

    test_exit();
    return 0;
}
