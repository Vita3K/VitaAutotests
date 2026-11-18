// sceIoDevctl on ux0: and sceIoIoctl on a file and a directory, with the sizes and commands that fail
#include "async_io.h"

#include <psp2/io/dirent.h>
#include <psp2/io/devctl.h>

#define DIR_PATH "ux0:data/vita-autotests/io_devctl"
#define FILE_PATH DIR_PATH "/file"

static void clean(void) {
    sceIoRemove(FILE_PATH);
    sceIoRmdir(DIR_PATH);
}

static unsigned word(const unsigned char *data, int offset) {
    unsigned value;
    sceClibMemcpy(&value, data + offset, sizeof(value));
    return value;
}

int main(void) {
    clean();
    sceIoMkdir(DIR_PATH, 0777);
    async_make_file(FILE_PATH, "0123", 4);

    // The free space query with a short and a long output
    unsigned char info[0x40];
    sceClibMemset(info, 0xAB, sizeof(info));
    PRINT("devctl 0x3001 with a 0x18 byte output", sceIoDevctl("ux0:", 0x3001, NULL, 0, info, 0x18));
    PRINT("word at +0x14", word(info, 0x14));
    PRINT("word at +0x18 is untouched", word(info, 0x18) == 0xABABABAB);
    const unsigned long long total = ((unsigned long long)word(info, 4) << 32) | word(info, 0);
    const unsigned long long free_bytes = ((unsigned long long)word(info, 12) << 32) | word(info, 8);
    const unsigned cluster = word(info, 0x10);
    PRINT("free is not above the total", free_bytes <= total);
    PRINT("the cluster size is a power of two", cluster != 0 && (cluster & (cluster - 1)) == 0);
    PRINT("the total is a multiple of the cluster size", cluster != 0 && total % cluster == 0);
    PRINT("free is a multiple of the cluster size", cluster != 0 && free_bytes % cluster == 0);

    unsigned char info28[0x40];
    sceClibMemset(info28, 0xAB, sizeof(info28));
    PRINT("devctl 0x3001 with a 0x28 byte output", sceIoDevctl("ux0:", 0x3001, NULL, 0, info28, 0x28));
    PRINT("the same total", word(info28, 0) == word(info, 0) && word(info28, 4) == word(info, 4));
    PRINT("word at +0x14 with 0x28 bytes", word(info28, 0x14));
    int spaces = 0;
    for (int i = 0x1C; i < 0x27; i++)
        spaces += info28[i] == ' ';
    PRINT("volume label bytes that are spaces", spaces);
    PRINT("byte at +0x27", info28[0x27]);
    PRINT("byte at +0x28 is untouched", info28[0x28] == 0xAB);

    PRINT("devctl 0x3001 with a 0x10 byte output", sceIoDevctl("ux0:", 0x3001, NULL, 0, info, 0x10));
    PRINT("devctl 0x3001 with a 0x30 byte output", sceIoDevctl("ux0:", 0x3001, NULL, 0, info, 0x30));
    PRINT("devctl 0x3001 with no output", sceIoDevctl("ux0:", 0x3001, NULL, 0, NULL, 0));
    PRINT("devctl 0x3004", sceIoDevctl("ux0:", 0x3004, info, 0x18, NULL, 0));
    PRINT("devctl 0x3005", sceIoDevctl("ux0:", 0x3005, NULL, 0, NULL, 0));
    PRINT("devctl 0x3401", sceIoDevctl("ux0:", 0x3401, NULL, 0, NULL, 0));
    PRINT("devctl 0x3002", sceIoDevctl("ux0:", 0x3002, NULL, 0, NULL, 0));
    PRINT("devctl 0x3802", sceIoDevctl("ux0:", 0x3802, NULL, 0, NULL, 0));
    PRINT("devctl 0x80000001", sceIoDevctl("ux0:", 0x80000001, NULL, 0, info, 8));
    PRINT("devctl on a missing device", sceIoDevctl("nodev0:", 0x3001, NULL, 0, info, 0x18));
    PRINT("devctl on a file path", sceIoDevctl(FILE_PATH, 0x3001, NULL, 0, info, 0x18));

    // The buffer cache query on a file
    const SceUID fd = sceIoOpen(FILE_PATH, SCE_O_RDONLY, 0);
    unsigned out[8];
    sceClibMemset(out, 0xAB, sizeof(out));
    out[6] = 0;
    PRINT("ioctl 0x1002 with a 0x20 byte output", sceIoIoctl(fd, 0x1002, NULL, 0, out, 0x20));
    for (int i = 0; i < 6; i++)
        PRINTF(out[i], "buffer cache word %d", i);
    PRINT("the word after is untouched", out[7] == 0xABABABAB);
    sceClibMemset(out, 0xAB, sizeof(out));
    out[6] = 1;
    PRINT("ioctl 0x1002 with word 6 set", sceIoIoctl(fd, 0x1002, NULL, 0, out, 0x20));
    sceClibMemset(out, 0, sizeof(out));
    PRINT("ioctl 0x1002 with a 0x1C byte output", sceIoIoctl(fd, 0x1002, NULL, 0, out, 0x1C));
    PRINT("ioctl 0x1002 with no output", sceIoIoctl(fd, 0x1002, NULL, 0, NULL, 0));
    PRINT("ioctl 0x201", sceIoIoctl(fd, 0x201, NULL, 0, out, 12));
    PRINT("ioctl 0x1800", sceIoIoctl(fd, 0x1800, NULL, 0, out, 4));
    PRINT("ioctl 0x1001 with no input", sceIoIoctl(fd, 0x1001, NULL, 0, NULL, 0));
    PRINT("ioctl 0x8000", sceIoIoctl(fd, 0x8000, NULL, 0, NULL, 0));
    PRINT("ioctl on a bad descriptor", sceIoIoctl(-1, 0x1002, NULL, 0, out, 0x20));
    sceIoClose(fd);
    PRINT("ioctl 0x1002 after closing", sceIoIoctl(fd, 0x1002, NULL, 0, out, 0x20));

    const SceUID dir = sceIoDopen(DIR_PATH);
    sceClibMemset(out, 0, sizeof(out));
    PRINT("ioctl 0x1002 on a directory", sceIoIoctl(dir, 0x1002, NULL, 0, out, 0x20));
    sceIoDclose(dir);

    clean();
    test_exit();
    return 0;
}
