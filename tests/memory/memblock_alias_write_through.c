// Writes through the mappings of one memory block, which the aliases made with
// sceKernelOpenMemBlock share with the original. Each kind is an ENABLE_OPEN 64 KiB block of
// USER_RW, USER_RW_UNCACHE, USER_CDIALOG_RW or USER_CDIALOG_NC_RW, with two aliases opened with
// flags 0x30 and one opened with 0x10. Each check stores a new value through one mapping and reads
// it through another, at three words: the first word, the last word, and a word over the first
// page boundary. The checks are:
// - a store through a 0x30 alias, read through the original
// - a store through the original, read through a 0x30 alias
// - a store through a 0x30 alias, read through the second 0x30 alias
// - for the aligned words, a kernel write of the block's base into a word of a 0x30 alias, read
//   through the original, and a kernel write into a word of the original, read through the 0x10
//   alias. The 0x10 alias is left out for a kind that doesn't open with it.
// A word over the page boundary is stored and read one byte at a time, since it isn't aligned.
// Each kernel write sets back the word it wrote over, and the stores all go to the block the test
// made. Each line is a comparison, 1 when the value read is the one written and 0 otherwise.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define SIZE (64 * KIB)
#define ENABLE_OPEN 0x4000
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

// A mapping of the block, the original or an alias, with its UID and base address
typedef struct Mapping {
    SceUID uid;
    volatile SceUInt8 *base;
} Mapping;

// The mappings of one block, which the name labels
typedef struct Kind {
    const char *name;
    Mapping original;
    Mapping writer;
    Mapping second;
    Mapping reader;
} Kind;

// The words checked, by their offset from the block's base
static const struct {
    const char *name;
    SceSize offset;
} positions[] = {
    { "first word", 0 },
    { "last word", SIZE - 4 },
    { "page edge", 4 * KIB - 2 },
};

static const struct {
    const char *name;
    SceUInt32 type;
} kinds[] = {
    { "USER_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW },
    { "USER_RW_UNCACHE", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE },
    { "USER_CDIALOG_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_RW },
    { "USER_CDIALOG_NC_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_NC_RW },
};

static SceUInt32 stamp = 0x5A000000;

// Returns a value that no word of the blocks holds yet
static SceUInt32 next_value(void) {
    return ++stamp;
}

static volatile SceUInt8 *base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return base;
}

// Opens an alias of the block by name with the flags, and sets its base if it opened
static void open_mapping(Mapping *mapping, const char *name, SceUInt32 flags) {
    mapping->uid = sceKernelOpenMemBlock(name, flags);
    mapping->base = mapping->uid >= 0 ? base_of(mapping->uid) : NULL;
}

// Stores value at offset from base, as one word when the offset is aligned, else byte by byte
static void store_word(volatile SceUInt8 *base, SceSize offset, SceUInt32 value) {
    volatile SceUInt8 *at = base + offset;
    if (offset % 4) {
        for (int i = 0; i < 4; i++)
            at[i] = value >> (8 * i);
    } else {
        *(volatile SceUInt32 *)at = value;
    }
}

// Loads the word at offset from base, the way store_word stores it
static SceUInt32 load_word(volatile SceUInt8 *base, SceSize offset) {
    volatile SceUInt8 *at = base + offset;
    if (offset % 4) {
        SceUInt32 value = 0;
        for (int i = 0; i < 4; i++)
            value |= (SceUInt32)at[i] << (8 * i);
        return value;
    }
    return *(volatile SceUInt32 *)at;
}

// Has the kernel write the base of block into the word at offset from base
static void kernel_write_base(SceUID block, volatile SceUInt8 *base, SceSize offset) {
    sceKernelGetMemBlockBase(block, (void **)(void *)(base + offset));
}

// The kernel writes at an aligned word, through the 0x30 alias and through the original
static void check_kernel_writes(const Kind *kind, const char *where, SceSize offset) {
    const SceUInt32 saved = load_word(kind->original.base, offset);
    kernel_write_base(kind->writer.uid, kind->writer.base, offset);
    PRINTF(load_word(kind->original.base, offset) == (SceUInt32)(SceUIntPtr)kind->writer.base,
           "%s, %s: kernel write through the 0x30 alias, read through the original", kind->name,
           where);
    store_word(kind->original.base, offset, saved);

    if (kind->reader.uid >= 0) {
        kernel_write_base(kind->original.uid, kind->original.base, offset);
        PRINTF(load_word(kind->reader.base, offset)
                   == (SceUInt32)(SceUIntPtr)kind->original.base,
               "%s, %s: kernel write through the original, read through the 0x10 alias",
               kind->name, where);
        store_word(kind->original.base, offset, saved);
    }
}

// Checks the stores and reads between the mappings at one word
static void check_offset(const Kind *kind, const char *where, SceSize offset) {
    SceUInt32 value = next_value();
    store_word(kind->writer.base, offset, value);
    PRINTF(load_word(kind->original.base, offset) == value,
           "%s, %s: store through the 0x30 alias, read through the original", kind->name, where);

    value = next_value();
    store_word(kind->original.base, offset, value);
    PRINTF(load_word(kind->writer.base, offset) == value,
           "%s, %s: store through the original, read through the 0x30 alias", kind->name, where);

    value = next_value();
    store_word(kind->writer.base, offset, value);
    PRINTF(load_word(kind->second.base, offset) == value,
           "%s, %s: store through the 0x30 alias, read through the second 0x30 alias", kind->name,
           where);

    if (offset % 4 == 0)
        check_kernel_writes(kind, where, offset);
}

// Opens the aliases of one block of the kind, checks each word, then closes them and frees it
static void check_kind(Kind *kind, SceUInt32 type) {
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    const SceUID original = sceKernelAllocMemBlock("write through", type, SIZE,
                                                   (SceKernelAllocMemBlockOpt *)&opt);
    PRINTF(test_created(original), "%s: allocate the openable block", kind->name);
    if (original < 0)
        return;
    kind->original.uid = original;
    kind->original.base = base_of(original);
    open_mapping(&kind->writer, "write through", 0x30);
    PRINTF(test_created(kind->writer.uid), "%s: open a 0x30 alias", kind->name);
    open_mapping(&kind->second, "write through", 0x30);
    PRINTF(test_created(kind->second.uid), "%s: open a second 0x30 alias", kind->name);
    open_mapping(&kind->reader, "write through", 0x10);
    PRINTF(test_created(kind->reader.uid), "%s: open a 0x10 alias", kind->name);

    if (kind->writer.uid >= 0 && kind->second.uid >= 0) {
        for (unsigned int i = 0; i < COUNT(positions); i++)
            check_offset(kind, positions[i].name, positions[i].offset);
    }

    if (kind->reader.uid >= 0)
        sceKernelCloseMemBlock(kind->reader.uid);
    if (kind->second.uid >= 0)
        sceKernelCloseMemBlock(kind->second.uid);
    if (kind->writer.uid >= 0)
        sceKernelCloseMemBlock(kind->writer.uid);
    sceKernelFreeMemBlock(original);
}

int main(void) {
    for (unsigned int i = 0; i < COUNT(kinds); i++) {
        Kind kind = { .name = kinds[i].name };
        check_kind(&kind, kinds[i].type);
    }
    test_exit();
    return 0;
}
