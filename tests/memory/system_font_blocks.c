// The shared blocks the shell keeps the system fonts in, which ScePsp2Compat and libpvf open by name:
// which open flags they take, what the lookup and info calls report about them, and whether they hold
// the font files from sa0:. Addresses and UIDs are printed as comparisons, never values.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400

static const struct {
    const char *name;
    const char *file;
} fonts[] = {
    { "SceSysDefaultLtnFont", "sa0:data/font/pvf/ltn0.pvf" },
    { "SceSysDefaultJpnFont", "sa0:data/font/pvf/jpn0.pvf" },
};

// The flags vitasdk lists for sceKernelOpenMemBlock, and 0
static const int open_flags[] = { 0, 0x10, 0x30 };

static char chunk[4 * KIB];

// Prints whether the block at base holds the file at path
static void compare_with_file(const char *name, const char *path, const char *base, SceSize mapped_size) {
    SceIoStat stat;
    const int ret = sceIoGetstat(path, &stat);
    PRINTF(ret, "%s: stat the font file", name);
    if (ret < 0)
        return;
    const SceSize file_size = (SceSize)stat.st_size;
    PRINTF(mapped_size == ((file_size + 4 * KIB - 1) & ~(4 * KIB - 1)), "%s: the mapped size is the file size rounded up to 4 KiB", name);
    PRINTF(mapped_size >= file_size, "%s: the file fits in the block", name);
    if (mapped_size < file_size)
        return;

    const SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    PRINTF(test_created(fd), "%s: open the font file", name);
    if (fd < 0)
        return;
    int same = 1;
    SceSize offset = 0;
    while (offset < file_size) {
        const int read = sceIoRead(fd, chunk, sizeof(chunk));
        if (read <= 0) {
            same = 0;
            break;
        }
        if (sceClibMemcmp(base + offset, chunk, read) != 0)
            same = 0;
        offset += read;
    }
    sceIoClose(fd);
    PRINTF(same, "%s: the block holds the file", name);
}

static void check_font(const char *name, const char *path) {
    // Every flag, closing what opens, then the first one that opens for the checks below
    int flags = -1;
    for (unsigned int i = 0; i < sizeof(open_flags) / sizeof(open_flags[0]); i++) {
        const SceUID uid = sceKernelOpenMemBlock(name, open_flags[i]);
        PRINTF(test_created(uid), "%s: open with flags 0x%X", name, open_flags[i]);
        if (uid >= 0) {
            PRINTF(sceKernelCloseMemBlock(uid), "%s: close the block opened with flags 0x%X", name, open_flags[i]);
            if (flags < 0)
                flags = open_flags[i];
        }
    }
    if (flags < 0)
        return;

    const SceUID uid = sceKernelOpenMemBlock(name, flags);
    PRINTF(test_created(uid), "%s: open", name);
    if (uid < 0)
        return;
    const SceUID again = sceKernelOpenMemBlock(name, flags);
    PRINTF(test_created(again), "%s: open it again", name);
    PRINTF(again == uid, "%s: both opens give the same UID", name);

    void *base = NULL;
    PRINTF(sceKernelGetMemBlockBase(uid, &base), "%s: get the base", name);
    PRINTF(base != NULL, "%s: the base isn't NULL", name);
    PRINTF((SceUIntPtr)base % (4 * KIB) == 0, "%s: the base is aligned to 4 KiB", name);
    void *base_again = NULL;
    sceKernelGetMemBlockBase(again, &base_again);
    PRINTF(base_again == base, "%s: the second open has the same base", name);
    PRINTF(sceKernelFindMemBlockByAddr(base, 0) == uid, "%s: find by the base gives the opened UID", name);

    SceKernelMemBlockInfo info = { .size = sizeof(info) };
    PRINTF(sceKernelGetMemBlockInfoByAddr(base, &info), "%s: info by address", name);
    PRINTF(info.mappedBase == base, "%s: its mapped base is the base", name);
    PRINTF(info.mappedSize, "%s: its mapped size", name);
    PRINTF(info.memoryType, "%s: its memory type", name);
    PRINTF(info.access, "%s: its access", name);
    PRINTF(info.type, "%s: its type", name);
    PRINTF(*(volatile SceUInt32 *)base, "%s: its first word", name);

    compare_with_file(name, path, base, info.mappedSize);

    PRINTF(sceKernelCloseMemBlock(again), "%s: close the second open", name);
    PRINTF(sceKernelCloseMemBlock(uid), "%s: close", name);
    PRINTF(sceKernelCloseMemBlock(uid), "%s: close it again", name);
    PRINTF(sceKernelGetMemBlockBase(uid, &base), "%s: get the base after closing", name);
    PRINTF(sceKernelFreeMemBlock(sceKernelOpenMemBlock(name, flags)), "%s: free an opened block", name);
    PRINTF(test_created(sceKernelOpenMemBlock(name, flags)), "%s: open after that", name);
}

int main(void) {
    for (unsigned int i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++)
        check_font(fonts[i].name, fonts[i].file);

    // Names that aren't the fonts', with the flags the fonts open with, since a Vita refuses 0 first
    PRINT("open SceSysDefaultLtnFon", sceKernelOpenMemBlock("SceSysDefaultLtnFon", 0x10));
    PRINT("open scesysdefaultltnfont", sceKernelOpenMemBlock("scesysdefaultltnfont", 0x10));
    PRINT("open an empty name", sceKernelOpenMemBlock("", 0x10));
    const SceUID own = sceKernelAllocMemBlock("vita-autotests own", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
    PRINT("open a block of the test by its name", test_created(sceKernelOpenMemBlock("vita-autotests own", 0x10)));
    sceKernelFreeMemBlock(own);

    // A NULL name last, so an implementation that crashes on it loses nothing else
    PRINT("open a NULL name", sceKernelOpenMemBlock(NULL, 0x10));

    test_exit();
    return 0;
}
