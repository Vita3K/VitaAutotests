// Completing and cancelling async requests, before and after their result is taken
#include "async_io.h"

#define DIR_PATH "ux0:data/vita-autotests/io_async_complete"
#define FILE_PATH DIR_PATH "/file"

static void clean(void) {
    sceIoRemove(FILE_PATH);
    sceIoRmdir(DIR_PATH);
}

int main(void) {
    clean();
    sceIoMkdir(DIR_PATH, 0777);
    async_make_file(FILE_PATH, "0123456789abcdef", 16);
    const SceUID fd = sceIoOpen(FILE_PATH, SCE_O_RDONLY, 0);
    char buf[8];

    // Complete before the result was taken
    SceUID uid = sceIoReadAsync(fd, buf, 4, NULL);
    PRINT("read async", uid > 0);
    async_wait_set(uid);
    SceUInt32 pattern = 0;
    PRINT("the event's pattern call", sceKernelGetEventPattern(uid, &pattern));
    PRINT("the result is set", pattern);
    PRINT("complete without taking the result", sceIoComplete(uid));
    SceUInt32 got = 0;
    SceUInt64 user_data = 0;
    PRINT("poll for the result", sceKernelPollEvent(uid, 1, &got, &user_data));
    PRINT("the polled pattern", got);
    PRINT("the polled result", (unsigned)user_data);
    PRINT("poll again", sceKernelPollEvent(uid, 1, &got, &user_data));
    PRINT("complete after taking the result", sceIoComplete(uid));
    PRINT("complete again", sceIoComplete(uid));

    // Cancel a finished request
    uid = sceIoReadAsync(fd, buf, 4, NULL);
    async_print("read async", async_wait(uid));
    PRINT("cancel a finished request", sceIoCancel(uid));
    PRINT("complete it", sceIoComplete(uid));
    PRINT("cancel it after completing", sceIoCancel(uid));

    // Cancel something that is not a request
    PRINT("cancel a file descriptor", sceIoCancel(fd));
    PRINT("complete a file descriptor", sceIoComplete(fd));

    // Cancel right after the submit. Whether it still queued is a race, so print what must hold
    uid = sceIoReadAsync(fd, buf, 4, NULL);
    const int cancel = sceIoCancel(uid);
    int consistent;
    if (cancel == 0) {
        // The cancel took the request out, so its UID is gone
        consistent = sceIoComplete(uid) == (int)0x80028201;
    } else {
        async_wait(uid);
        consistent = sceIoComplete(uid) == 0;
    }
    PRINT("a cancel either removes the request or leaves it to finish", consistent);

    sceIoClose(fd);
    clean();
    test_exit();
    return 0;
}
