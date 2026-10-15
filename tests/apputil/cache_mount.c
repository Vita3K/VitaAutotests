// The cache0: device of sceAppUtilCacheMount: what it takes to mount and unmount it, what cache0: does
// while it isn't mounted, where its files go, and whether they stay across mounts. The test empties it
// first.
#include <psp2/apputil.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include "test.h"

#define FILE_A "cache0:a.bin"
#define SUB "cache0:sub"
// Where cache0: is expected to be on ux0:
#define UX0_CACHE "ux0:cache/VTST00000"

// Returns the size of path, or the error
static int file_size(const char *path) {
    SceIoStat stat;
    const int ret = sceIoGetstat(path, &stat);
    return ret < 0 ? ret : (int)stat.st_size;
}

// Returns how many entries path lists, or the error
static int count_entries(const char *path) {
    const SceUID dir = sceIoDopen(path);
    if (dir < 0)
        return dir;
    int count = 0;
    SceIoDirent entry;
    while (sceIoDread(dir, &entry) > 0)
        count++;
    sceIoDclose(dir);
    return count;
}

// Prints what each call does on cache0:, under label
static void try_access(const char *label) {
    PRINTF(file_size(FILE_A), "%s: stat a file", label);
    PRINTF(test_created(sceIoOpen(FILE_A, SCE_O_RDONLY, 0)), "%s: open a file to read", label);
    PRINTF(count_entries("cache0:"), "%s: list cache0:", label);
    PRINTF(sceIoMkdir(SUB, 0777), "%s: create a directory", label);
    sceIoRmdir(SUB);
}

int main(void) {
    PRINT("mount before init", sceAppUtilCacheMount());
    PRINT("unmount before init", sceAppUtilCacheUmount());

    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    sceClibMemset(&init, 0, sizeof(init));
    sceClibMemset(&boot, 0, sizeof(boot));
    PRINT("init", sceAppUtilInit(&init, &boot));

    // Not mounted
    PRINT("unmount before mounting", sceAppUtilCacheUmount());
    PRINT("create a file before mounting", test_created(sceIoOpen(FILE_A, SCE_O_WRONLY | SCE_O_CREAT, 0666)));
    try_access("before mounting");

    PRINT("mount", sceAppUtilCacheMount());
    sceIoRemove(FILE_A);
    sceIoRemove(SUB "/b.bin");
    sceIoRmdir(SUB);
    PRINT("mount again", sceAppUtilCacheMount());

    // Mounted
    SceIoStat stat;
    PRINT("stat cache0:", sceIoGetstat("cache0:", &stat));
    PRINT("cache0: is a directory", SCE_S_ISDIR(stat.st_mode));
    PRINT("entries in the emptied cache0:", count_entries("cache0:"));
    SceUID fd = sceIoOpen(FILE_A, SCE_O_WRONLY | SCE_O_CREAT, 0666);
    PRINT("create a file", test_created(fd));
    PRINT("write 10 bytes", sceIoWrite(fd, "0123456789", 10));
    sceIoClose(fd);
    char buf[16];
    fd = sceIoOpen(FILE_A, SCE_O_RDONLY, 0);
    PRINT("open it to read", test_created(fd));
    PRINT("read it", sceIoRead(fd, buf, sizeof(buf)));
    PRINT("it holds what was written", sceClibMemcmp(buf, "0123456789", 10) == 0);
    sceIoClose(fd);
    PRINT("stat it with a slash after the device", file_size("cache0:/a.bin"));
    PRINT("create a directory", sceIoMkdir(SUB, 0777));
    fd = sceIoOpen(SUB "/b.bin", SCE_O_WRONLY | SCE_O_CREAT, 0666);
    PRINT("create a file in it", test_created(fd));
    sceIoClose(fd);
    PRINT("entries in cache0:", count_entries("cache0:"));
    PRINT("stat the file on ux0:", file_size(UX0_CACHE "/a.bin"));
    PRINT("list the directory on ux0:", count_entries(UX0_CACHE));
    PRINT("rename the file", sceIoRename(FILE_A, "cache0:c.bin"));
    PRINT("rename it back", sceIoRename("cache0:c.bin", FILE_A));

    // Unmounted again
    PRINT("unmount", sceAppUtilCacheUmount());
    PRINT("unmount again", sceAppUtilCacheUmount());
    try_access("after unmounting");
    PRINT("stat the file on ux0: after unmounting", file_size(UX0_CACHE "/a.bin"));

    // Mounted again, the files are still there
    PRINT("mount after unmounting", sceAppUtilCacheMount());
    PRINT("the file is still there", file_size(FILE_A));
    PRINT("the file in the directory is still there", file_size(SUB "/b.bin"));
    PRINT("remove the file", sceIoRemove(FILE_A));
    PRINT("remove the file in the directory", sceIoRemove(SUB "/b.bin"));
    PRINT("remove the directory", sceIoRmdir(SUB));
    PRINT("entries left", count_entries("cache0:"));
    PRINT("unmount at the end", sceAppUtilCacheUmount());

    sceAppUtilShutdown();
    PRINT("unmount after shutdown", sceAppUtilCacheUmount());
    test_exit();
    return 0;
}
