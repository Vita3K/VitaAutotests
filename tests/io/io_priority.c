// The io priority calls. A game sees the internal priority minus 6 and may set 0 to 4.
#include "async_io.h"

#define DIR_PATH "ux0:data/vita-autotests/io_priority"
#define FILE_PATH DIR_PATH "/file"

int sceIoGetPriority(SceUID fd);
int sceIoSetPriority(SceUID fd, int priority);
int sceIoGetProcessDefaultPriority(void);
int sceIoSetProcessDefaultPriority(int priority);
int sceIoGetThreadDefaultPriority(void);
int sceIoSetThreadDefaultPriority(int priority);

static void clean(void) {
    sceIoRemove(FILE_PATH);
    sceIoRmdir(DIR_PATH);
}

int main(void) {
    clean();
    sceIoMkdir(DIR_PATH, 0777);
    async_make_file(FILE_PATH, "0123", 4);

    PRINT("process default priority", sceIoGetProcessDefaultPriority());
    PRINT("thread default priority", sceIoGetThreadDefaultPriority());
    SceUID fd = sceIoOpen(FILE_PATH, SCE_O_RDONLY, 0);
    PRINT("priority of a new descriptor", sceIoGetPriority(fd));

    PRINT("set 5", sceIoSetPriority(fd, 5));
    PRINT("priority after set 5", sceIoGetPriority(fd));
    PRINT("set -1", sceIoSetPriority(fd, -1));
    PRINT("set 15", sceIoSetPriority(fd, 15));
    PRINT("set 0", sceIoSetPriority(fd, 0));
    PRINT("priority after set 0", sceIoGetPriority(fd));
    PRINT("set 4", sceIoSetPriority(fd, 4));
    PRINT("priority after set 4", sceIoGetPriority(fd));
    PRINT("set 2", sceIoSetPriority(fd, 2));
    PRINT("priority after set 2", sceIoGetPriority(fd));

    // A request with a changed priority still works
    char buf[4];
    const SceUID uid = sceIoReadAsync(fd, buf, 4, NULL);
    async_print("read async at priority 2", async_wait(uid));
    sceIoComplete(uid);

    // The defaults
    PRINT("set the thread default to 5", sceIoSetThreadDefaultPriority(5));
    PRINT("set the thread default to 4", sceIoSetThreadDefaultPriority(4));
    PRINT("thread default priority after 4", sceIoGetThreadDefaultPriority());
    SceUID other = sceIoOpen(FILE_PATH, SCE_O_RDONLY, 0);
    PRINT("priority of a descriptor opened after it", sceIoGetPriority(other));
    sceIoClose(other);
    PRINT("set the process default to 5", sceIoSetProcessDefaultPriority(5));
    PRINT("set the process default to -1", sceIoSetProcessDefaultPriority(-1));
    PRINT("set the process default to 0", sceIoSetProcessDefaultPriority(0));
    PRINT("process default priority after 0", sceIoGetProcessDefaultPriority());
    PRINT("thread default priority after the process one", sceIoGetThreadDefaultPriority());

    // Bad descriptors
    sceIoClose(fd);
    PRINT("get on a closed descriptor", sceIoGetPriority(fd));
    PRINT("set on a closed descriptor", sceIoSetPriority(fd, 1));
    PRINT("get on a bad descriptor", sceIoGetPriority(-1));

    clean();
    test_exit();
    return 0;
}
