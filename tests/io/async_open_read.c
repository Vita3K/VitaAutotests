// Async open, read and close return an event UID at once, and the event holds the result
#include "async_io.h"

#define DIR_PATH "ux0:data/vita-autotests/io_async_open"
#define FILE_PATH DIR_PATH "/file"

static void clean(void) {
    sceIoRemove(FILE_PATH);
    sceIoRmdir(DIR_PATH);
}

int main(void) {
    clean();
    sceIoMkdir(DIR_PATH, 0777);
    async_make_file(FILE_PATH, "0123456789abcdef", 16);

    // Open an existing file
    SceUID uid = sceIoOpenAsync(FILE_PATH, SCE_O_RDONLY, 0, NULL);
    PRINT("open async of an existing file", uid > 0);
    AsyncResult result = async_wait(uid);
    PRINT("open async wait", result.wait);
    PRINT("open async pattern", result.pattern);
    PRINT("open async result", test_created((SceUID)result.user_data));
    PRINT("open async result high word", (int)(result.user_data >> 32));
    const SceUID fd = (SceUID)result.user_data;
    char buf[8] = { 0 };
    PRINT("read from the opened descriptor", sceIoRead(fd, buf, 4));
    PRINT("it read the file's first bytes", sceClibStrncmp(buf, "0123", 4) == 0);
    PRINT("wait again for the same event", async_wait(uid).wait);
    PRINT("complete it", sceIoComplete(uid));
    PRINT("complete it again", sceIoComplete(uid));
    PRINT("close the descriptor", sceIoClose(fd));

    // Open a missing file
    uid = sceIoOpenAsync(DIR_PATH "/missing", SCE_O_RDONLY, 0, NULL);
    PRINT("open async of a missing file", uid > 0);
    async_print("open async of a missing file", async_wait(uid));
    PRINT("complete it", sceIoComplete(uid));

    // Errors found at once
    PRINT("open async of a null path", sceIoOpenAsync(NULL, SCE_O_RDONLY, 0, NULL));
    PRINT("open async of a path with no device", sceIoOpenAsync("nodevice", SCE_O_RDONLY, 0, NULL));
    PRINT("read async of a closed descriptor", sceIoReadAsync(fd, buf, 4, NULL));
    PRINT("close async of a closed descriptor", sceIoCloseAsync(fd, NULL));
    PRINT("complete a descriptor", sceIoComplete(fd));

    // Two reads on one descriptor
    const SceUID file = sceIoOpen(FILE_PATH, SCE_O_RDONLY, 0);
    char first[8] = { 0 }, second[8] = { 0 };
    const SceUID uid1 = sceIoReadAsync(file, first, 4, NULL);
    const SceUID uid2 = sceIoReadAsync(file, second, 4, NULL);
    PRINT("first read async", uid1 > 0);
    PRINT("second read async", uid2 > 0);
    PRINT("the two events differ", uid1 != uid2);
    const AsyncResult result1 = async_wait(uid1);
    const AsyncResult result2 = async_wait(uid2);
    async_print("first read", result1);
    async_print("second read", result2);
    PRINT("the first read got the first bytes", sceClibStrncmp(first, "0123", 4) == 0);
    PRINT("the second read got the next bytes", sceClibStrncmp(second, "4567", 4) == 0);
    PRINT("complete the first", sceIoComplete(uid1));
    PRINT("complete the second", sceIoComplete(uid2));

    // Close async
    uid = sceIoCloseAsync(file, NULL);
    PRINT("close async", uid > 0);
    async_print("close async", async_wait(uid));
    PRINT("read from the closed descriptor", sceIoRead(file, first, 4));
    PRINT("complete it", sceIoComplete(uid));

    // Write async
    uid = sceIoOpenAsync(FILE_PATH, SCE_O_WRONLY | SCE_O_TRUNC, 0, NULL);
    const SceUID out = (SceUID)async_wait(uid).user_data;
    sceIoComplete(uid);
    uid = sceIoWriteAsync(out, "xyz", 3, NULL);
    PRINT("write async", uid > 0);
    async_print("write async", async_wait(uid));
    sceIoComplete(uid);
    sceIoClose(out);
    SceIoStat stat;
    PRINT("stat the written file", sceIoGetstat(FILE_PATH, &stat));
    PRINT("its size", (int)stat.st_size);

    clean();
    test_exit();
    return 0;
}
