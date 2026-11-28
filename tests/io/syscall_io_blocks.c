// The file manager calls behind pread, pwrite, ioctl and devctl with a NULL or unmapped argument
// block, and devctl with an unmapped input. Unmapped means the first page of the address space.
//
// On a PS Vita TV on firmware 3.65 a bad block gives 0x80022005, as the call copies it in before
// anything else, so also for an ioctl on a closed descriptor. An unmapped devctl input gives
// 0x80022005 for command 0x3001, on a device and on a missing one, so the input is copied in before
// the device is looked up. Command 0x3401 gives 0x80010030, its command bits are refused before
// the copy. A refused pwrite leaves the file as it was.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "syscalls.h"
#include "test.h"

#define IO_DIR "ux0:data/vita-autotests/io"
#define FILE_A IO_DIR "/blocks_a.bin"

int main(void) {
    char buf[16] = { 0 };
    SceUInt32 words[8] = { 0 };
    const void *const unmapped = (const void *)0x10;
    sceIoMkdir(IO_DIR, 0777);
    const SceUID fd = sceIoOpen(FILE_A, SCE_O_RDWR | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    PRINT("open a file", test_created(fd));
    PRINT("write 10 bytes", sceIoWrite(fd, "0123456789", 10));

    PRINT("pread with no offset block", syscall_io_pread(fd, buf, 4, NULL));
    PRINT("pread with an unmapped offset block", syscall_io_pread(fd, buf, 4, unmapped));
    PRINT("pwrite with no offset block", syscall_io_pwrite(fd, "ab", 2, NULL));
    PRINT("pwrite with an unmapped offset block", syscall_io_pwrite(fd, "ab", 2, unmapped));
    PRINT("pread with an offset block", syscall_io_pread(fd, buf, 10, &(IoOffsetBlock){ 0, 0 }));
    PRINTF(sceClibMemcmp(buf, "0123456789", 10) == 0, "the file is still 0123456789");

    const IoControlBlock ioctl_block = { 0, words, sizeof(words) };
    PRINT("ioctl 0x1002 with no block", syscall_io_ioctl(fd, 0x1002, NULL, NULL));
    PRINT("ioctl 0x1002 with an unmapped block", syscall_io_ioctl(fd, 0x1002, NULL, unmapped));
    PRINT("ioctl 0x1002 with a block", syscall_io_ioctl(fd, 0x1002, NULL, &ioctl_block));
    sceIoClose(fd);
    PRINT("ioctl on a closed descriptor with no block", syscall_io_ioctl(fd, 0x1002, NULL, NULL));

    const IoControlBlock devctl_block = { 4, NULL, 0 };
    PRINT("devctl 0x3001 with no block", syscall_io_devctl("ux0:", 0x3001, NULL, NULL));
    PRINT("devctl 0x3001 with an unmapped block", syscall_io_devctl("ux0:", 0x3001, NULL, unmapped));
    PRINT("devctl 0x3001 with an unmapped input", syscall_io_devctl("ux0:", 0x3001, unmapped, &devctl_block));
    PRINT("devctl on a missing device with an unmapped input", syscall_io_devctl("xx0:", 0x3001, unmapped, &devctl_block));
    PRINT("devctl 0x3401 with an unmapped input", syscall_io_devctl("ux0:", 0x3401, unmapped, &devctl_block));

    sceIoRemove(FILE_A);
    test_exit();
    return 0;
}
