// The file manager calls behind libkernel's sceIo exports, called with the arguments libkernel passes:
// an option block that is never filled in, the 64 bit offset of pread and pwrite as a two word
// block, and the lengths and output buffer of devctl and ioctl in a block as the fourth argument.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "syscalls.h"
#include "test.h"

#define DIR_PATH "ux0:data/vita-autotests/syscall_io"
#define FILE_A DIR_PATH "/a.bin"
#define FILE_B DIR_PATH "/b.bin"
#define SUB_DIR DIR_PATH "/sub"

static const IoOptBlock opt = { { 0 } };

static int exists(const char *path) {
    SceIoStat stat;
    return sceIoGetstat(path, &stat) == 0;
}

int main(void) {
    // Leftovers of an earlier run
    sceIoRemove(FILE_A);
    sceIoRemove(FILE_B);
    sceIoRmdir(SUB_DIR);
    sceIoRmdir(DIR_PATH);

    PRINT("make the test directory", syscall_io_mkdir(DIR_PATH, 0777, &opt));
    SceUID fd = sceIoOpen(FILE_A, SCE_O_RDWR | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    PRINT("write 10 bytes", sceIoWrite(fd, "0123456789", 10));

    // pread and pwrite at an offset, the file position doesn't move
    char buf[8] = { 0 };
    const IoOffsetBlock at4 = { 4, 0 };
    PRINT("pread 4 bytes at 4", syscall_io_pread(fd, buf, 4, &at4));
    PRINT("they are 4567", sceClibMemcmp(buf, "4567", 4) == 0);
    const IoOffsetBlock at2 = { 2, 0 };
    PRINT("pwrite 2 bytes at 2", syscall_io_pwrite(fd, "AB", 2, &at2));
    sceClibMemset(buf, 0, sizeof(buf));
    PRINT("pread them back", syscall_io_pread(fd, buf, 4, &(IoOffsetBlock){ 0, 0 }));
    PRINT("the file starts with 01AB", sceClibMemcmp(buf, "01AB", 4) == 0);
    PRINT("the position is still at the end", sceIoLseek(fd, 0, SCE_SEEK_CUR));
    const IoOffsetBlock far = { 0, 1 };
    PRINT("pread at 4 GiB", syscall_io_pread(fd, buf, 4, &far));

    // ioctl on the file
    SceUInt32 words[8];
    sceClibMemset(words, 0xAB, sizeof(words));
    const IoControlBlock ioctl_block = { 0, words, sizeof(words) };
    // Word 6 of the output must be 0, so a filled buffer is refused
    PRINT("ioctl 0x1002 into a filled buffer", syscall_io_ioctl(fd, 0x1002, NULL, &ioctl_block));
    sceClibMemset(words, 0, sizeof(words));
    PRINT("ioctl 0x1002", syscall_io_ioctl(fd, 0x1002, NULL, &ioctl_block));
    for (int i = 0; i < 6; i++)
        PRINTF(words[i], "ioctl 0x1002: word %d", i);
    PRINT("ioctl 0x8000", syscall_io_ioctl(fd, 0x8000, NULL, &ioctl_block));
    sceIoClose(fd);
    PRINT("ioctl on a closed descriptor", syscall_io_ioctl(fd, 0x1002, NULL, &ioctl_block));

    // devctl on the device
    SceUInt32 space[8];
    sceClibMemset(space, 0xAB, sizeof(space));
    const IoControlBlock devctl_block = { 0, space, 0x18 };
    PRINT("devctl 0x3001 on ux0:", syscall_io_devctl("ux0:", 0x3001, NULL, &devctl_block));
    PRINT("devctl 0x3005 on ux0:", syscall_io_devctl("ux0:", 0x3005, NULL, &devctl_block));
    PRINT("devctl on a missing device", syscall_io_devctl("xx0:", 0x3001, NULL, &devctl_block));

    // Sync, rename, remove and rmdir
    PRINT("sync ux0:", syscall_io_sync("ux0:", 0, &opt));
    PRINT("rename a.bin to b.bin", syscall_io_rename(FILE_A, FILE_B, &opt));
    PRINT("a.bin is gone", !exists(FILE_A));
    PRINT("b.bin is there", exists(FILE_B));
    PRINT("rename a missing file", syscall_io_rename(FILE_A, FILE_B, &opt));
    PRINT("remove b.bin", syscall_io_remove(FILE_B, &opt));
    PRINT("b.bin is gone", !exists(FILE_B));
    PRINT("remove it again", syscall_io_remove(FILE_B, &opt));
    PRINT("make a subdirectory", syscall_io_mkdir(SUB_DIR, 0777, &opt));
    PRINT("remove it", syscall_io_rmdir(SUB_DIR, &opt));
    PRINT("remove it again", syscall_io_rmdir(SUB_DIR, &opt));
    PRINT("remove the test directory", syscall_io_rmdir(DIR_PATH, &opt));

    test_exit();
    return 0;
}
