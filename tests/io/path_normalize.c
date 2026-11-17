// Path normalization: dot, dot-dot, multiple slashes, backslashes and root escape
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include "test.h"

#define BASE_DIR "ux0:data/vita-autotests/io"
#define SUB_DIR BASE_DIR "/subdir"
#define FILE_NORM BASE_DIR "/norm.txt"

static void check_open(const char *label, const char *path) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    PRINTF(test_created(fd), "%s: %s", label, path);
    if (fd >= 0)
        sceIoClose(fd);
}

int main(void) {
    sceIoMkdir("ux0:data/vita-autotests/io", 0777);
    sceIoMkdir(SUB_DIR, 0777);

    SceUID fd = sceIoOpen(FILE_NORM, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    sceIoWrite(fd, "test", 4);
    sceIoClose(fd);

    // 1. Basic dot and dot-dot
    check_open("exact", FILE_NORM);
    check_open("single dot", BASE_DIR "/./norm.txt");
    check_open("double dot", SUB_DIR "/../norm.txt");
    check_open("nested dot dot", SUB_DIR "/./.././norm.txt");

    // 2. Multiple slashes
    check_open("double slash", BASE_DIR "//norm.txt");
    check_open("triple slash", BASE_DIR "///norm.txt");
    check_open("slash after device", "ux0://data/vita-autotests/io/norm.txt");

    // 3. Backslash
    check_open("backslash separator", BASE_DIR "\\norm.txt");
    check_open("backslash dot-dot", SUB_DIR "\\..\\norm.txt");

    // 4. Traversal beyond device root
    check_open("escape ux0 root", "ux0:../norm.txt");
    check_open("escape ux0 deep", "ux0:../../../../norm.txt");

    // 5. Trailing slash and dot on file
    check_open("file with trailing slash", FILE_NORM "/");
    check_open("file with trailing dot", FILE_NORM "/.");
    check_open("file with trailing dot-dot", FILE_NORM "/..");

    sceIoRemove(FILE_NORM);
    sceIoRmdir(SUB_DIR);
    test_exit();
    return 0;
}

