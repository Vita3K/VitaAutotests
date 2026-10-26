// Which memory a kernel call may write and read for the app, per kind of memory block. A kernel
// call that writes or reads through an address the app gives it is used as a probe, instead of the
// app touching that memory itself, since the app may not be allowed to use it. The write probe is
// sceKernelGetMemBlockBase into a word of the block, which puts the block's base there, and the
// read probe sends one byte into a message pipe. The read probe sends exactly one byte, so its
// result doesn't depend on the bytes after it. The cases:
// - each of the 16 types an app may allocate, at its unit, from a plain block of that type
// - an alias of each of those types, opened read only with 0x10 and read and write with 0x30, from
//   a block allocated with ENABLE_OPEN
// - a block for code from sceKernelAllocMemBlockForVM, probed with the VM domain closed, then open
//   from this thread, from a thread started before the domain was opened and from one started
//   after it
// - the app's own memory: a const string, an initialized global, a local on the main thread's
//   stack, TLS slot 0 of the main thread and a block from malloc
// Each block prints its access, the write and the read of its last word and byte, then the info by
// range and the find by range over the whole block. A write only aims at a word the test owns, and
// a word it changed gets its old value back when the block says the app may write it. The app
// loads a word only after the kernel has read its byte, and stores it only after the kernel has
// written it and the block says it may be written, so no probe stores into the code, the stack
// local or TLS slot 0.
#include <stdlib.h>

#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define KIB 0x400
#define MIB 0x100000
#define ENABLE_OPEN 0x4000
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define RESTORE 1
#define KEEP 0
// The access bit of the info call that says the app may write the block
#define ACCESS_WRITE 0x2

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

// Not declared in the headers, so declared here
int sceKernelFreeMemBlockForVM(SceUID uid);

// The 16 types an app may allocate, with the unit a block of each takes
static const struct {
    const char *name;
    SceUInt32 type;
    SceSize unit;
} types[] = {
    { "USER_CDRAM_L1WBWA_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_L1WBWA_RW, 256 * KIB },
    { "USER_CDRAM_R", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_R, 256 * KIB },
    { "USER_CDRAM_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB },
    { "USER_MAIN_DEVICE_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_DEVICE_RW, 4 * KIB },
    { "USER_MAIN_R", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_R, 4 * KIB },
    { "USER_MAIN_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_RW, 4 * KIB },
    { "USER_MAIN_NC_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_NC_RW, 4 * KIB },
    { "USER_MAIN_GAME_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_GAME_RW, 4 * KIB },
    { "USER_MAIN_PHYCONT_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, MIB },
    { "USER_MAIN_PHYCONT_NC_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, MIB },
    { "USER_MAIN_CDIALOG_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_CDIALOG_RW, 4 * KIB },
    { "USER_MAIN_CDIALOG_NC_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_CDIALOG_NC_RW, 4 * KIB },
    { "USER_CDIALOG_R", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_R, 4 * KIB },
    { "USER_CDIALOG_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_RW, 4 * KIB },
    { "USER_CDIALOG_NC_R", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_NC_R, 4 * KIB },
    { "USER_CDIALOG_NC_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_NC_RW, 4 * KIB },
};

// What the probes and the lookups of one block give
typedef struct BlockCheck {
    SceKernelMemBlockInfo info; // the block of the probed byte, from its info by address
    int wrote;                  // whether a write probe was made
    int write;                  // the write probe's result
    int read;                   // the read probe's result
    int range;                  // info by range over the whole block
    int find;                   // find by range over the whole block, 0 for a UID
} BlockCheck;

// The last word of a block of size bytes at base
static SceUInt32 *last_word(void *base, SceSize size) {
    return (SceUInt32 *)((char *)base + size - 4);
}

// Returns 0 when the kernel can read the byte at addr for the app, or the error
static int kernel_read(const void *addr) {
    const SceUID pipe = sceKernelCreateMsgPipe("block probe", MSG_PIPE_TYPE_USER, 0, 4 * KIB, NULL);
    if (pipe < 0)
        return pipe;
    const int result = sceKernelTrySendMsgPipe(pipe, (void *)addr, 1, MSG_PIPE_MODE_FULL, NULL);
    sceKernelDeleteMsgPipe(pipe);
    return result;
}

// Returns 0 when the kernel can write the word for the app, or the error. A write that works puts
// the base of uid's block in the word.
static int kernel_write(SceUID uid, SceUInt32 *word) {
    return sceKernelGetMemBlockBase(uid, (void **)word);
}

// Probes the byte with a kernel read, and the word with a kernel write through uid when word isn't
// NULL. When restore is set and the info call says the app may write the block, a word the write
// changed gets its old value back. The app loads the word only after the kernel read the byte, and
// stores it only after the kernel wrote the word.
static BlockCheck probe_block(SceUID uid, SceUInt32 *word, const void *byte, int restore) {
    BlockCheck c = { .info = { .size = sizeof(c.info) }, .wrote = word != NULL };
    const int info = sceKernelGetMemBlockInfoByAddr((void *)byte, &c.info);
    c.read = kernel_read(byte);
    const int own = restore && word != NULL && c.read == 0 && (c.info.access & ACCESS_WRITE);
    const SceUInt32 old = own ? *(volatile SceUInt32 *)word : 0;
    if (word != NULL)
        c.write = kernel_write(uid, word);
    if (own && c.write == 0)
        *(volatile SceUInt32 *)word = old;
    if (info < 0) {
        c.range = info;
        c.find = info;
        return c;
    }
    SceKernelMemBlockInfo whole = { .size = sizeof(whole) };
    c.range = sceKernelGetMemBlockInfoByRange(c.info.mappedBase, c.info.mappedSize, &whole);
    c.find = test_created(sceKernelFindMemBlockByAddr(c.info.mappedBase, c.info.mappedSize));
    return c;
}

// Prints a block's access, its write when one was made, its read, and its lookups
static void print_block(const char *name, const BlockCheck *c) {
    PRINTF(c->info.access, "%s: its access", name);
    if (c->wrote)
        PRINTF(c->write, "%s: the write", name);
    PRINTF(c->read, "%s: the read", name);
    PRINTF(c->range, "%s: info by range", name);
    PRINTF(c->find, "%s: find by range", name);
}

// Each of the 16 types from a plain block of its unit
static void plain_types(void) {
    for (unsigned int i = 0; i < COUNT(types); i++) {
        const SceUID block
            = sceKernelAllocMemBlock("access type", types[i].type, types[i].unit, NULL);
        PRINTF(test_created(block), "%s: allocate it", types[i].name);
        if (block < 0)
            continue;
        void *base = NULL;
        sceKernelGetMemBlockBase(block, &base);
        const BlockCheck c = probe_block(block, last_word(base, types[i].unit),
            (char *)base + types[i].unit - 1, RESTORE);
        print_block(types[i].name, &c);
        sceKernelFreeMemBlock(block);
    }
}

// An alias of each type, from an ENABLE_OPEN block of its unit, with flags 0x10 and 0x30
static void alias_types(void) {
    static const SceUInt32 flags[] = { 0x10, 0x30 };
    for (unsigned int i = 0; i < COUNT(types); i++) {
        AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
        const SceUID original = sceKernelAllocMemBlock("access alias", types[i].type,
            types[i].unit, (SceKernelAllocMemBlockOpt *)&opt);
        PRINTF(test_created(original), "%s: allocate it with ENABLE_OPEN", types[i].name);
        if (original < 0)
            continue;
        for (unsigned int j = 0; j < COUNT(flags); j++) {
            char name[96];
            sceClibSnprintf(name, sizeof(name), "%s alias 0x%X", types[i].name, flags[j]);
            const SceUID alias = sceKernelOpenMemBlock("access alias", flags[j]);
            PRINTF(test_created(alias), "%s: open", name);
            if (alias < 0)
                continue;
            void *base = NULL;
            sceKernelGetMemBlockBase(alias, &base);
            const BlockCheck c = probe_block(alias, last_word(base, types[i].unit),
                (char *)base + types[i].unit - 1, RESTORE);
            print_block(name, &c);
            sceKernelCloseMemBlock(alias);
        }
        sceKernelFreeMemBlock(original);
    }
}

static SceUID go, vm_block;
static void *vm_base;
static BlockCheck thread_before, thread_after;

// Probes the last word of the block for code, without restoring it: the app can't write the block
static BlockCheck code_probe(void) {
    return probe_block(vm_block, last_word(vm_base, MIB), (char *)vm_base + MIB - 1, KEEP);
}

// Probes the block for code from a thread that started before the domain was opened
static int probe_before(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(go, 1, &timeout);
    thread_before = code_probe();
    return 0;
}

// Probes the block for code from a thread that started after the domain was opened
static int probe_after(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    thread_after = code_probe();
    return 0;
}

// The block for code with the domain closed, open in this thread, and probed from two threads
static void code_block(void) {
    vm_block = sceKernelAllocMemBlockForVM("access code", MIB);
    PRINT("a block for code: allocate it", test_created(vm_block));
    if (vm_block < 0)
        return;
    sceKernelGetMemBlockBase(vm_block, &vm_base);

    const BlockCheck closed = code_probe();
    print_block("a block for code, the domain closed", &closed);

    go = sceKernelCreateSema("access code", 0, 0, 1, NULL);
    const SceUID before = test_start_thread(probe_before);
    test_wait_until_waiting(before);
    const int opened = sceKernelOpenVMDomain();
    const BlockCheck open = code_probe();
    sceKernelSignalSema(go, 1);
    test_join_thread(before);
    const SceUID after = test_start_thread(probe_after);
    test_join_thread(after);
    const int closed_again = sceKernelCloseVMDomain();
    sceKernelDeleteSema(go);

    PRINT("open the domain", opened);
    print_block("a block for code, the domain open", &open);
    print_block("a thread started before the open", &thread_before);
    print_block("a thread started after the open", &thread_after);
    PRINT("close the domain", closed_again);
    PRINT("free the block for code", sceKernelFreeMemBlockForVM(vm_block));
}

static const char text[] = "a const string the probes read";
static SceUInt32 data_word = 0x1234;

// The app's own memory: its code, its data, a local, TLS and malloc's memory
static void app_memory(void) {
    const SceUID helper
        = sceKernelAllocMemBlock("access helper", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);

    const BlockCheck literal = probe_block(helper, NULL, text, KEEP);
    print_block("a const string", &literal);

    const BlockCheck global = probe_block(helper, &data_word, (char *)&data_word + 3, RESTORE);
    print_block("an initialized global", &global);

    SceUInt32 local = 0;
    const BlockCheck on_stack = probe_block(helper, NULL, &local, KEEP);
    print_block("a local on the main thread's stack", &on_stack);

    const BlockCheck tls = probe_block(helper, NULL, sceKernelGetTLSAddr(0), KEEP);
    print_block("TLS slot 0 of the main thread", &tls);

    char *buf = malloc(64);
    if (buf != NULL) {
        const BlockCheck heap = probe_block(helper, (SceUInt32 *)(buf + 60), buf + 63, RESTORE);
        print_block("memory from malloc", &heap);
        free(buf);
    }

    sceKernelFreeMemBlock(helper);
}

int main(void) {
    plain_types();
    alias_types();
    code_block();
    app_memory();
    test_exit();
    return 0;
}
