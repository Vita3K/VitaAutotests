// Directories: creating and removing them, listing them, and the errors for each. Everything happens
// in IO_DIR, which the test empties first.
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "test.h"

#define IO_DIR "ux0:data/vita-autotests/dir"
#define SUB IO_DIR "/sub"

static void touch(const char *path) {
    const SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT, 0666);
    sceIoClose(fd);
}

int main(void) {
    sceIoRemove(IO_DIR "/a");
    sceIoRemove(IO_DIR "/b");
    sceIoRemove(SUB "/c");
    sceIoRmdir(SUB);
    sceIoRmdir(IO_DIR);

    // Creating and removing
    PRINT("create the directory", sceIoMkdir(IO_DIR, 0777));
    PRINT("create it again", sceIoMkdir(IO_DIR, 0777));
    PRINT("create one in a missing parent", sceIoMkdir(IO_DIR "/missing/x", 0777));
    PRINT("create one with a trailing slash", sceIoMkdir(SUB "/", 0777));
    PRINT("remove it", sceIoRmdir(SUB));
    PRINT("create one with mode 0", sceIoMkdir(SUB, 0));
    sceIoMkdir(SUB, 0777);
    touch(IO_DIR "/a");
    touch(IO_DIR "/b");
    touch(SUB "/c");
    PRINT("create one where a file is", sceIoMkdir(IO_DIR "/a", 0777));
    PRINT("remove a directory that isn't empty", sceIoRmdir(SUB));
    PRINT("remove a missing directory", sceIoRmdir(IO_DIR "/missing"));
    PRINT("remove a file as a directory", sceIoRmdir(IO_DIR "/a"));

    // Listing
    PRINT("open a missing directory", sceIoDopen(IO_DIR "/missing"));
    PRINT("open a file as a directory", sceIoDopen(IO_DIR "/a"));
    SceUID dir = sceIoDopen(IO_DIR);
    PRINT("open the directory", test_created(dir));
    int count = 0, dots = 0, files = 0, dirs = 0, found_a = 0, found_b = 0, found_sub = 0, ret;
    SceIoDirent entry;
    while ((ret = sceIoDread(dir, &entry)) > 0) {
        count++;
        if (sceClibStrcmp(entry.d_name, ".") == 0 || sceClibStrcmp(entry.d_name, "..") == 0)
            dots++;
        if (SCE_S_ISREG(entry.d_stat.st_mode))
            files++;
        if (SCE_S_ISDIR(entry.d_stat.st_mode))
            dirs++;
        found_a += sceClibStrcmp(entry.d_name, "a") == 0;
        found_b += sceClibStrcmp(entry.d_name, "b") == 0;
        found_sub += sceClibStrcmp(entry.d_name, "sub") == 0;
    }
    PRINT("what the last read returns", ret);
    PRINT("the entries", count);
    PRINT("the dot entries", dots);
    PRINT("the files", files);
    PRINT("the directories", dirs);
    PRINT("a is listed once", found_a);
    PRINT("b is listed once", found_b);
    PRINT("sub is listed once", found_sub);
    PRINT("read again after the end", sceIoDread(dir, &entry));
    PRINT("close it", sceIoDclose(dir));
    PRINT("close it again", sceIoDclose(dir));
    PRINT("read from it after closing", sceIoDread(dir, &entry));
    const SceUID file = sceIoOpen(IO_DIR "/a", SCE_O_RDONLY, 0);
    PRINT("read a file descriptor as a directory", sceIoDread(file, &entry));
    PRINT("close a file descriptor as a directory", sceIoDclose(file));
    sceIoClose(file);
    PRINT("close a directory as a file", sceIoClose(dir = sceIoDopen(IO_DIR)));
    sceIoDclose(dir);
    PRINT("open an empty path as a directory", sceIoDopen(""));

    // Removing everything
    sceIoRemove(SUB "/c");
    PRINT("remove the emptied directory", sceIoRmdir(SUB));
    sceIoRemove(IO_DIR "/a");
    sceIoRemove(IO_DIR "/b");
    PRINT("remove the top directory", sceIoRmdir(IO_DIR));

    test_exit();
    return 0;
}
