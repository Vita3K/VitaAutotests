// Writes through descriptors opened for reading only with CREAT or EXCL next to it, which don't add
// write access.
//
// On a PS Vita TV on firmware 3.65 the open makes the file and the write gives 0x8001000D.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "test.h"

#define IO_DIR "ux0:data/vita-autotests/io"
#define FILE_A IO_DIR "/rdonly_create_a.bin"

static int file_size(const char *path) {
    SceIoStat stat;
    const int ret = sceIoGetstat(path, &stat);
    return ret < 0 ? ret : (int)stat.st_size;
}

int main(void) {
    sceIoMkdir(IO_DIR, 0777);
    sceIoRemove(FILE_A);

    SceUID fd = sceIoOpen(FILE_A, SCE_O_RDONLY | SCE_O_CREAT, 0666);
    PRINT("open to read with CREAT", test_created(fd));
    PRINT("write through it", sceIoWrite(fd, "abcd", 4));
    PRINT("pwrite through it", sceIoPwrite(fd, "abcd", 4, 0));
    sceIoClose(fd);
    PRINT("the size", file_size(FILE_A));
    sceIoRemove(FILE_A);

    fd = sceIoOpen(FILE_A, SCE_O_RDONLY | SCE_O_CREAT | SCE_O_EXCL, 0666);
    PRINT("open to read with CREAT and EXCL", test_created(fd));
    PRINT("write through it", sceIoWrite(fd, "abcd", 4));
    sceIoClose(fd);
    PRINT("the size", file_size(FILE_A));

    sceIoRemove(FILE_A);
    test_exit();
    return 0;
}
