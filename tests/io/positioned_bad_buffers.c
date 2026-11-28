// pread and pwrite with a buffer in memory that isn't mapped, the first page of the address space,
// next to an offset at the size, a negative offset and a size of 0.
//
// On a PS Vita TV on firmware 3.65 an unmapped buffer gives 0x8001000E, also at the size and before
// the offset check, and a size of 0 gives 0 with any buffer. A refused pwrite leaves the file as it
// was.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "test.h"

#define IO_DIR "ux0:data/vita-autotests/io"
#define FILE_A IO_DIR "/bad_buffers_a.bin"

int main(void) {
    char buf[16] = { 0 };
    void *const unmapped = (void *)0x10;
    sceIoMkdir(IO_DIR, 0777);
    const SceUID fd = sceIoOpen(FILE_A, SCE_O_RDWR | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    PRINT("open a file", test_created(fd));
    PRINT("write 10 bytes", sceIoWrite(fd, "0123456789", 10));

    PRINT("pread into an unmapped buffer", sceIoPread(fd, unmapped, 4, 0));
    PRINT("pread into an unmapped buffer at the size", sceIoPread(fd, unmapped, 4, 10));
    PRINT("pread into an unmapped buffer at a negative offset", sceIoPread(fd, unmapped, 4, -1));
    PRINT("pread of 0 bytes into an unmapped buffer", sceIoPread(fd, unmapped, 0, 0));
    PRINT("pread of 0 bytes into a null buffer", sceIoPread(fd, NULL, 0, 0));
    PRINT("pwrite from an unmapped buffer", sceIoPwrite(fd, unmapped, 4, 0));
    PRINT("pwrite of 0 bytes from an unmapped buffer", sceIoPwrite(fd, unmapped, 0, 0));
    PRINT("pwrite of 0 bytes from a null buffer", sceIoPwrite(fd, NULL, 0, 0));
    PRINT("pread them back", sceIoPread(fd, buf, 10, 0));
    PRINTF(sceClibMemcmp(buf, "0123456789", 10) == 0, "the file is still 0123456789");
    PRINT("the size", sceIoLseek(fd, 0, SCE_SEEK_END));
    sceIoClose(fd);

    sceIoRemove(FILE_A);
    test_exit();
    return 0;
}
