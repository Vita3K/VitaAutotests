// APPEND writes through two descriptors of one file: each write goes to the end the other one left,
// and the position each descriptor reports afterwards. A pwrite and a plain write next to them
// show which writes the APPEND flag moves. Everything happens in IO_DIR.
//
// On a PS Vita TV on firmware 3.65 every APPEND write goes to the current end, whichever
// descriptor wrote last, and leaves the descriptor's position where it was. A pwrite writes at its
// offset and a descriptor without APPEND writes at its position.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "test.h"

#define IO_DIR "ux0:data/vita-autotests/io"
#define FILE_A IO_DIR "/append_a.bin"

// Returns the size of path, or the error
static int file_size(const char *path) {
    SceIoStat stat;
    const int ret = sceIoGetstat(path, &stat);
    return ret < 0 ? ret : (int)stat.st_size;
}

// The position of a descriptor
static int position(SceUID fd) {
    return (int)sceIoLseek(fd, 0, SCE_SEEK_CUR);
}

int main(void) {
    char buf[32];
    sceIoMkdir(IO_DIR, 0777);
    sceIoRemove(FILE_A);
    SceUID fd = sceIoOpen(FILE_A, SCE_O_WRONLY | SCE_O_CREAT, 0666);
    sceIoWrite(fd, "0123456789", 10);
    sceIoClose(fd);

    const SceUID one = sceIoOpen(FILE_A, SCE_O_WRONLY | SCE_O_APPEND, 0);
    const SceUID two = sceIoOpen(FILE_A, SCE_O_RDWR | SCE_O_APPEND, 0);
    PRINT("open it with APPEND", test_created(one));
    PRINT("open it again to read and write with APPEND", test_created(two));
    PRINT("the first descriptor's position", position(one));
    PRINT("write a through the first", sceIoWrite(one, "a", 1));
    PRINT("write b through the second", sceIoWrite(two, "b", 1));
    PRINT("write c through the first", sceIoWrite(one, "c", 1));
    PRINT("the size", file_size(FILE_A));
    PRINT("the first descriptor's position after it", position(one));
    PRINT("the second descriptor's position after it", position(two));

    PRINT("seek the first to 2", (int)sceIoLseek(one, 2, SCE_SEEK_SET));
    PRINT("write d through the first", sceIoWrite(one, "d", 1));
    PRINT("the first descriptor's position after it", position(one));
    PRINT("pwrite e at 1 through the second", sceIoPwrite(two, "e", 1, 1));
    PRINT("the second descriptor's position after it", position(two));
    PRINT("the size after both", file_size(FILE_A));

    // A descriptor without APPEND writes where it is
    const SceUID plain = sceIoOpen(FILE_A, SCE_O_WRONLY, 0);
    PRINT("write f at the start without APPEND", sceIoWrite(plain, "f", 1));
    PRINT("its position after it", position(plain));
    PRINT("write g through the first", sceIoWrite(one, "g", 1));
    sceIoClose(plain);
    sceIoClose(one);
    sceIoClose(two);

    PRINT("the size at the end", file_size(FILE_A));
    fd = sceIoOpen(FILE_A, SCE_O_RDONLY, 0);
    const int read = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    buf[read > 0 ? read : 0] = '\0';
    PRINTF(read, "the contents are %s", buf);

    sceIoRemove(FILE_A);
    test_exit();
    return 0;
}
