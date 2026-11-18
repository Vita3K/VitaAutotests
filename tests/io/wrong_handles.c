// sceIo calls given the wrong kind of descriptor or a UID that is not a descriptor, a stat that
// fails, and a create under a missing directory
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define HANDLES_DIR "ux0:data/vita-autotests/wrong_handles"
#define FILE_A HANDLES_DIR "/a.bin"

int main(void) {
    char buf[16];
    sceIoMkdir(HANDLES_DIR, 0777);
    sceIoRemove(FILE_A);
    sceIoRmdir(HANDLES_DIR "/missing/sub");
    sceIoRmdir(HANDLES_DIR "/missing");
    SceUID fd = sceIoOpen(FILE_A, SCE_O_WRONLY | SCE_O_CREAT, 0666);
    sceIoWrite(fd, "0123456789", 10);
    sceIoClose(fd);

    // A directory descriptor in the file calls
    const SceUID dir = sceIoDopen(HANDLES_DIR);
    PRINT("open the directory", test_created(dir));
    PRINT("seek on the directory", (int)sceIoLseek(dir, 0, SCE_SEEK_SET));
    PRINT("read from the directory", sceIoRead(dir, buf, sizeof(buf)));
    PRINT("write to the directory", sceIoWrite(dir, "x", 1));
    SceIoStat stat;
    PRINT("stat the directory by descriptor", sceIoGetstatByFd(dir, &stat));
    PRINT("close the directory", sceIoDclose(dir));

    // A semaphore UID in the descriptor calls
    const SceUID sema = sceKernelCreateSema("io sema", 0, 0, 1, NULL);
    PRINT("create a semaphore", test_created(sema));
    PRINT("read from the semaphore", sceIoRead(sema, buf, sizeof(buf)));
    PRINT("write to the semaphore", sceIoWrite(sema, "x", 1));
    PRINT("seek on the semaphore", (int)sceIoLseek(sema, 0, SCE_SEEK_SET));
    SceIoDirent dirent;
    PRINT("read the semaphore as a directory", sceIoDread(sema, &dirent));
    PRINT("close the semaphore as a file", sceIoClose(sema));
    PRINT("close the semaphore as a directory", sceIoDclose(sema));
    PRINT("the semaphore is still there", sceKernelSignalSema(sema, 1));
    PRINT("delete the semaphore", sceKernelDeleteSema(sema));

    // What a failed stat leaves in the buffer
    sceClibMemset(&stat, 0xCC, sizeof(stat));
    PRINT("stat a missing file", sceIoGetstat(HANDLES_DIR "/missing.bin", &stat));
    PRINT("the mode word after it", stat.st_mode);
    PRINT("the size after it", (int)stat.st_size);
    sceClibMemset(&stat, 0xCC, sizeof(stat));
    PRINT("stat an empty path", sceIoGetstat("", &stat));
    PRINT("the mode word after it", stat.st_mode);
    sceClibMemset(&stat, 0xCC, sizeof(stat));
    PRINT("stat a path without a device", sceIoGetstat("nodevice", &stat));
    PRINT("the mode word after it", stat.st_mode);

    // A create under a directory that does not exist
    fd = sceIoOpen(HANDLES_DIR "/missing/sub/a.bin", SCE_O_WRONLY | SCE_O_CREAT, 0666);
    PRINT("create a file under a missing directory", test_created(fd));
    if (fd >= 0)
        sceIoClose(fd);
    PRINT("stat the missing directory after it", sceIoGetstat(HANDLES_DIR "/missing", &stat));

    // Paths with spaces in front
    fd = sceIoOpen("  " FILE_A, SCE_O_RDONLY, 0);
    PRINT("open a path with two spaces in front", test_created(fd));
    if (fd >= 0)
        sceIoClose(fd);

    sceIoRemove(FILE_A);
    PRINT("remove the test directory", sceIoRmdir(HANDLES_DIR));
    test_exit();
    return 0;
}
