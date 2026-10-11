// Files: open flags, reads and writes past the ends, seeking, stat, renaming, removing, and the
// errors for each. Everything happens in IO_DIR, which the test empties first.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "test.h"

#define IO_DIR "ux0:data/vita-autotests/io"
#define FILE_A IO_DIR "/a.bin"
#define FILE_B IO_DIR "/b.bin"
#define MISSING IO_DIR "/missing.bin"

// Returns the size of path, or the error
static int file_size(const char *path) {
    SceIoStat stat;
    const int ret = sceIoGetstat(path, &stat);
    return ret < 0 ? ret : (int)stat.st_size;
}

int main(void) {
    char buf[64];
    sceIoMkdir(IO_DIR, 0777);
    sceIoRemove(FILE_A);
    sceIoRemove(FILE_B);
    sceIoRemove(MISSING);

    // Opening
    PRINT("open a missing file to read", sceIoOpen(MISSING, SCE_O_RDONLY, 0));
    PRINT("open a missing file to write without create", sceIoOpen(MISSING, SCE_O_WRONLY, 0));
    SceUID fd = sceIoOpen(FILE_A, SCE_O_WRONLY | SCE_O_CREAT, 0666);
    PRINT("create a file", test_created(fd));
    PRINT("write 10 bytes", sceIoWrite(fd, "0123456789", 10));
    PRINT("close it", sceIoClose(fd));
    PRINT("close it again", sceIoClose(fd));
    PRINT("write to the closed file", sceIoWrite(fd, "x", 1));
    PRINT("create it again with EXCL", sceIoOpen(FILE_A, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_EXCL, 0666));
    PRINT("open with no access mode", test_created(fd = sceIoOpen(FILE_A, 0, 0)));
    if (fd >= 0)
        sceIoClose(fd);
    PRINT("open a directory as a file", sceIoOpen(IO_DIR, SCE_O_RDONLY, 0));
    PRINT("open an empty path", sceIoOpen("", SCE_O_RDONLY, 0));
    PRINT("open a path without a device", sceIoOpen("data/vita-autotests/io/a.bin", SCE_O_RDONLY, 0));
    PRINT("open a path on an unknown device", sceIoOpen("xx0:a.bin", SCE_O_RDONLY, 0));
    PRINT("open a file under a file", sceIoOpen(FILE_A "/x", SCE_O_RDONLY, 0));

    // Reading
    fd = sceIoOpen(FILE_A, SCE_O_RDONLY, 0);
    PRINT("write to a file opened to read", sceIoWrite(fd, "x", 1));
    PRINT("read 4 bytes", sceIoRead(fd, buf, 4));
    PRINT("they are the first 4", sceClibMemcmp(buf, "0123", 4) == 0);
    PRINT("read 0 bytes", sceIoRead(fd, buf, 0));
    PRINT("read past the end", sceIoRead(fd, buf, sizeof(buf)));
    PRINT("read at the end", sceIoRead(fd, buf, sizeof(buf)));

    // Seeking
    PRINT("seek to 2", (int)sceIoLseek(fd, 2, SCE_SEEK_SET));
    PRINT("seek 3 more", (int)sceIoLseek(fd, 3, SCE_SEEK_CUR));
    PRINT("seek to the end", (int)sceIoLseek(fd, 0, SCE_SEEK_END));
    PRINT("seek to one before the end", (int)sceIoLseek(fd, -1, SCE_SEEK_END));
    PRINT("seek before the start", (int)sceIoLseek(fd, -1, SCE_SEEK_SET));
    PRINT("the position after it", (int)sceIoLseek(fd, 0, SCE_SEEK_CUR));
    PRINT("seek past the end", (int)sceIoLseek(fd, 100, SCE_SEEK_SET));
    PRINT("read there", sceIoRead(fd, buf, 4));
    PRINT("seek with whence 3", (int)sceIoLseek(fd, 0, 3));
    PRINT("read 2 at offset 5", sceIoPread(fd, buf, 2, 5));
    PRINT("they are the bytes there", sceClibMemcmp(buf, "56", 2) == 0);
    PRINT("the position after it", (int)sceIoLseek(fd, 0, SCE_SEEK_CUR));
    PRINT("read at an offset past the end", sceIoPread(fd, buf, 2, 100));
    sceIoClose(fd);

    // Writing
    fd = sceIoOpen(FILE_A, SCE_O_WRONLY, 0);
    PRINT("read from a file opened to write", sceIoRead(fd, buf, 1));
    PRINT("seek past the end to write", (int)sceIoLseek(fd, 16, SCE_SEEK_SET));
    PRINT("write there", sceIoWrite(fd, "x", 1));
    sceIoClose(fd);
    PRINT("the file grew", file_size(FILE_A));
    fd = sceIoOpen(FILE_A, SCE_O_RDONLY, 0);
    sceIoPread(fd, buf, 6, 10);
    PRINT("the gap reads as zeros", sceClibMemcmp(buf, "\0\0\0\0\0\0", 6) == 0);
    sceIoClose(fd);
    fd = sceIoOpen(FILE_A, SCE_O_WRONLY | SCE_O_APPEND, 0);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    PRINT("write 3 bytes in append mode after seeking to 0", sceIoWrite(fd, "abc", 3));
    sceIoClose(fd);
    PRINT("the size after it", file_size(FILE_A));
    fd = sceIoOpen(FILE_A, SCE_O_RDONLY, 0);
    sceIoRead(fd, buf, 1);
    PRINT("the first byte is unchanged", buf[0] == '0');
    sceIoClose(fd);
    PRINT("write at an offset", sceIoPwrite(fd = sceIoOpen(FILE_A, SCE_O_WRONLY, 0), "Z", 1, 0));
    sceIoClose(fd);
    fd = sceIoOpen(FILE_A, SCE_O_RDWR | SCE_O_TRUNC, 0);
    PRINT("open with TRUNC", test_created(fd));
    sceIoClose(fd);
    PRINT("the size after it", file_size(FILE_A));
    fd = sceIoOpen(FILE_A, SCE_O_WRONLY, 0);
    sceIoWrite(fd, "0123456789", 10);
    PRINT("write 0 bytes", sceIoWrite(fd, buf, 0));
    sceIoClose(fd);

    // Stat
    SceIoStat stat;
    PRINT("stat the file", sceIoGetstat(FILE_A, &stat));
    PRINT("it is a regular file", SCE_S_ISREG(stat.st_mode));
    PRINT("its size", (int)stat.st_size);
    PRINT("stat a missing file", sceIoGetstat(MISSING, &stat));
    PRINT("stat the directory", sceIoGetstat(IO_DIR, &stat));
    PRINT("it is a directory", SCE_S_ISDIR(stat.st_mode));
    fd = sceIoOpen(FILE_A, SCE_O_RDONLY, 0);
    PRINT("stat by descriptor", sceIoGetstatByFd(fd, &stat));
    PRINT("its size", (int)stat.st_size);
    sceIoClose(fd);
    PRINT("stat a closed descriptor", sceIoGetstatByFd(fd, &stat));

    // Renaming and removing
    PRINT("rename a missing file", sceIoRename(MISSING, FILE_B));
    PRINT("rename the file", sceIoRename(FILE_A, FILE_B));
    PRINT("the old name is gone", file_size(FILE_A));
    PRINT("the new name has it", file_size(FILE_B));
    fd = sceIoOpen(FILE_A, SCE_O_WRONLY | SCE_O_CREAT, 0666);
    sceIoClose(fd);
    PRINT("rename over an existing file", sceIoRename(FILE_B, FILE_A));
    PRINT("the target's size after it", file_size(FILE_A));
    PRINT("rename it to itself", sceIoRename(FILE_A, FILE_A));
    PRINT("remove a missing file", sceIoRemove(MISSING));
    PRINT("remove a directory", sceIoRemove(IO_DIR));
    fd = sceIoOpen(FILE_A, SCE_O_RDONLY, 0);
    PRINT("remove an open file", sceIoRemove(FILE_A));
    PRINT("read from it after the remove", sceIoRead(fd, buf, 4));
    sceIoClose(fd);
    sceIoRemove(FILE_A);
    sceIoRemove(FILE_B);

    // Descriptors that aren't files
    PRINT("read from an unknown descriptor", sceIoRead(0x12345, buf, 1));
    PRINT("close an unknown descriptor", sceIoClose(0x12345));
    PRINT("close descriptor 0", sceIoClose(0));

    test_exit();
    return 0;
}
