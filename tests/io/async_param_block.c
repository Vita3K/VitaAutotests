// The parameter block of an async call, which the system fills in when the request ends
#include "async_io.h"

#define DIR_PATH "ux0:data/vita-autotests/io_async_param"
#define FILE_PATH DIR_PATH "/file"

static void clean(void) {
    sceIoRemove(FILE_PATH);
    sceIoRmdir(DIR_PATH);
}

static void print_block(const char *label, const unsigned *block) {
    PRINTF(block[0], "%s result low word", label);
    PRINTF(block[1], "%s result high word", label);
    PRINTF(block[2], "%s saved word", label);
    PRINTF(block[3] == 0x11111111, "%s fourth word is untouched", label);
    PRINTF(block[3] == 0, "%s fourth word is zero", label);
    PRINTF(block[4] == 0x11111111, "%s fifth word is untouched", label);
    PRINTF(block[4] == 0, "%s fifth word is zero", label);
    PRINTF(block[5], "%s last word", label);
}

int main(void) {
    clean();
    sceIoMkdir(DIR_PATH, 0777);
    async_make_file(FILE_PATH, "0123456789abcdef", 16);
    const SceUID fd = sceIoOpen(FILE_PATH, SCE_O_RDONLY, 0);
    char buf[8];

    // A read with a block
    unsigned block[6];
    sceClibMemset(block, 0x11, sizeof(block));
    SceUID uid = sceIoReadAsync(fd, buf, 4, block);
    PRINT("read async with a block", uid > 0);
    AsyncResult result = async_wait(uid);
    PRINT("wait", result.wait);
    PRINT("the pattern", result.pattern);
    PRINT("the event's user data is the block's address", (unsigned)result.user_data == (unsigned)block);
    PRINT("its high word", (unsigned)(result.user_data >> 32));
    print_block("read", block);
    sceIoComplete(uid);

    // A failed open with a block
    sceClibMemset(block, 0x11, sizeof(block));
    uid = sceIoOpenAsync(DIR_PATH "/missing", SCE_O_RDONLY, 0, block);
    result = async_wait(uid);
    PRINT("the user data of the failed open is the block's address", (unsigned)result.user_data == (unsigned)block);
    print_block("failed open", block);
    sceIoComplete(uid);

    // An open with a block
    sceClibMemset(block, 0x11, sizeof(block));
    uid = sceIoOpenAsync(FILE_PATH, SCE_O_RDONLY, 0, block);
    result = async_wait(uid);
    PRINT("the user data of the open is the block's address", (unsigned)result.user_data == (unsigned)block);
    PRINT("the result in the block is a descriptor", sceIoClose((SceUID)block[0]));
    PRINT("the result high word", block[1]);
    PRINT("the saved word", block[2]);
    sceIoComplete(uid);

    // A request that gets no block
    const SceUID file = sceIoOpen(FILE_PATH, SCE_O_RDONLY, 0);
    uid = sceIoReadAsync(file, buf, 4, NULL);
    result = async_wait(uid);
    PRINT("without a block the user data is the result", (unsigned)result.user_data);
    sceIoComplete(uid);
    sceIoClose(file);

    clean();
    test_exit();
    return 0;
}
