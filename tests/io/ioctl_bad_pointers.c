// The buffer cache ioctls with an input or output pointer into memory that isn't mapped, the first
// page of the address space.
//
// On a PS Vita TV on firmware 3.65 both give 0x80022005: the call copies the input and the output in
// from the caller after the descriptor checks and fails on the copy.
#include <psp2/io/devctl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "test.h"

#define IO_DIR "ux0:data/vita-autotests/io"
#define FILE_A IO_DIR "/ioctl_a.bin"

int main(void) {
    sceIoMkdir(IO_DIR, 0777);
    const SceUID fd = sceIoOpen(FILE_A, SCE_O_RDWR | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    PRINT("open a file", test_created(fd));

    void *const unmapped = (void *)0x10;
    PRINT("ioctl 0x1001 with an unmapped input", sceIoIoctl(fd, 0x1001, unmapped, 0x1C, NULL, 0));
    PRINT("ioctl 0x1002 with an unmapped output", sceIoIoctl(fd, 0x1002, NULL, 0, unmapped, 0x20));

    sceIoClose(fd);
    sceIoRemove(FILE_A);
    test_exit();
    return 0;
}
