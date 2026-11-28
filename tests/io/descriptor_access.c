// Reads and writes through a descriptor opened without that access, and writes through two
// descriptors of one file, read back through a third before the writers close.
//
// On a PS Vita TV on firmware 3.65 a read without read access and a write without write access give
// 0x8001000D, also with APPEND. A plain write is seen at once by the other descriptors, an APPEND
// write on another descriptor goes after it, and the next plain write goes at its own position,
// over the appended byte.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "test.h"

#define IO_DIR "ux0:data/vita-autotests/io"
#define FILE_A IO_DIR "/access_a.bin"

// Reads the whole file through fd from the start and prints it
static void print_contents(const char *what, SceUID fd) {
    char buf[32];
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    const int read = sceIoRead(fd, buf, sizeof(buf) - 1);
    buf[read > 0 ? read : 0] = '\0';
    PRINTF(read, "%s: %s", what, buf);
}

int main(void) {
    char buf[8];
    sceIoMkdir(IO_DIR, 0777);
    sceIoRemove(FILE_A);
    SceUID fd = sceIoOpen(FILE_A, SCE_O_WRONLY | SCE_O_CREAT, 0666);
    PRINT("write seed", sceIoWrite(fd, "seed", 4));
    sceIoClose(fd);

    const SceUID reader = sceIoOpen(FILE_A, SCE_O_RDONLY, 0);
    const SceUID writer = sceIoOpen(FILE_A, SCE_O_WRONLY, 0);
    const SceUID appender = sceIoOpen(FILE_A, SCE_O_WRONLY | SCE_O_APPEND, 0);
    PRINT("open it to read", test_created(reader));
    PRINT("open it to write", test_created(writer));
    PRINT("open it to append", test_created(appender));

    PRINT("write through the read descriptor", sceIoWrite(reader, "x", 1));
    PRINT("read through the write descriptor", sceIoRead(writer, buf, 1));
    PRINT("read through the APPEND descriptor", sceIoRead(appender, buf, 1));
    print_contents("the file after the refused calls", reader);

    PRINT("seek the write descriptor to 4", (int)sceIoLseek(writer, 4, SCE_SEEK_SET));
    PRINT("write PP through it", sceIoWrite(writer, "PP", 2));
    print_contents("the reader sees", reader);
    PRINT("append A", sceIoWrite(appender, "A", 1));
    print_contents("the reader sees after the append", reader);
    PRINT("write Q through the write descriptor", sceIoWrite(writer, "Q", 1));
    print_contents("the reader sees after Q", reader);

    sceIoClose(writer);
    sceIoClose(appender);
    print_contents("the file after the writers close", reader);
    sceIoClose(reader);

    sceIoRemove(FILE_A);
    test_exit();
    return 0;
}
