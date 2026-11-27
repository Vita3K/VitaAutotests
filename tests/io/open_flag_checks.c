// The open flag combinations sceIoOpen refuses, the order of those checks against a missing file,
// and what TRUNC does to an existing and to a new file. Everything happens in IO_DIR, which the
// test empties first.
//
// On a PS Vita TV on firmware 3.65 APPEND with TRUNC, no access mode, and read only with APPEND
// or TRUNC give 0x80010016 and make no file, a missing file without CREAT gives 0x80010002 before
// the access mode check, and TRUNC empties an existing file.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "test.h"

#define IO_DIR "ux0:data/vita-autotests/io"
#define FILE_A IO_DIR "/flags_a.bin"
#define FILE_NEW IO_DIR "/flags_new.bin"
#define MISSING IO_DIR "/flags_missing.bin"

// Returns the size of path, or the error
static int file_size(const char *path) {
    SceIoStat stat;
    const int ret = sceIoGetstat(path, &stat);
    return ret < 0 ? ret : (int)stat.st_size;
}

// Opens path with flags, prints the result and closes a descriptor it got
static void open_close(const char *what, const char *path, int flags) {
    const SceUID fd = sceIoOpen(path, flags, 0666);
    PRINT(what, test_created(fd));
    if (fd >= 0)
        sceIoClose(fd);
}

// Makes FILE_A 16 bytes long
static void fill_a(void) {
    const SceUID fd = sceIoOpen(FILE_A, SCE_O_WRONLY | SCE_O_CREAT, 0666);
    sceIoWrite(fd, "0123456789ABCDEF", 16);
    sceIoClose(fd);
}

int main(void) {
    sceIoMkdir(IO_DIR, 0777);
    sceIoRemove(FILE_A);
    sceIoRemove(FILE_NEW);
    sceIoRemove(MISSING);
    fill_a();

    // An existing file
    open_close("open with APPEND and TRUNC", FILE_A, SCE_O_WRONLY | SCE_O_APPEND | SCE_O_TRUNC);
    PRINT("the size after it", file_size(FILE_A));
    open_close("open to read with TRUNC", FILE_A, SCE_O_RDONLY | SCE_O_TRUNC);
    PRINT("the size after it", file_size(FILE_A));
    open_close("open to read with APPEND", FILE_A, SCE_O_RDONLY | SCE_O_APPEND);
    open_close("open with only APPEND", FILE_A, SCE_O_APPEND);
    open_close("open to read and write with APPEND", FILE_A, SCE_O_RDWR | SCE_O_APPEND);
    open_close("open to write with TRUNC", FILE_A, SCE_O_WRONLY | SCE_O_TRUNC);
    PRINT("the size after it", file_size(FILE_A));
    fill_a();
    open_close("open to read and write with TRUNC", FILE_A, SCE_O_RDWR | SCE_O_TRUNC);
    PRINT("the size after it", file_size(FILE_A));

    // A missing file
    open_close("open a missing file with APPEND and TRUNC", MISSING, SCE_O_WRONLY | SCE_O_APPEND | SCE_O_TRUNC);
    open_close("open a missing file to read with TRUNC", MISSING, SCE_O_RDONLY | SCE_O_TRUNC);
    open_close("open a missing file with no access mode", MISSING, 0);
    open_close("create a file with no access mode", MISSING, SCE_O_CREAT);
    PRINT("the file after it", file_size(MISSING));
    open_close("create a file to read with TRUNC", MISSING, SCE_O_RDONLY | SCE_O_CREAT | SCE_O_TRUNC);
    PRINT("the file after it", file_size(MISSING));
    open_close("create a file to write with TRUNC", FILE_NEW, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC);
    PRINT("its size", file_size(FILE_NEW));

    sceIoRemove(FILE_A);
    sceIoRemove(FILE_NEW);
    sceIoRemove(MISSING);
    test_exit();
    return 0;
}
