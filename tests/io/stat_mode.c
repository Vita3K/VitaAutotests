// The st_mode, st_attr and directory sizes sceIoGetstat reports on app0: and ux0:, and for files
// created with different modes
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include "test.h"

#define STAT_DIR "ux0:data/vita-autotests/stat"

static void stat_path(const char *label, const char *path, int show_size) {
    SceIoStat stat;
    sceClibMemset(&stat, 0xCC, sizeof(stat));
    const int ret = sceIoGetstat(path, &stat);
    PRINTF(ret, "stat %s", label);
    if (ret < 0)
        return;
    PRINTF(stat.st_mode, "%s mode", label);
    PRINTF(stat.st_attr, "%s attributes", label);
    if (show_size)
        PRINTF((int)stat.st_size, "%s size", label);
}

static void create_file(const char *path, int mode) {
    sceIoRemove(path);
    const SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, mode);
    sceIoWrite(fd, "stat", 4);
    sceIoClose(fd);
}

int main(void) {
    sceIoMkdir(STAT_DIR, 0777);
    sceIoMkdir(STAT_DIR "/dir", 0777);
    sceIoRmdir(STAT_DIR "/dir0");
    sceIoMkdir(STAT_DIR "/dir0", 0);
    create_file(STAT_DIR "/file", 0666);
    create_file(STAT_DIR "/file0", 0);
    create_file(STAT_DIR "/file777", 0777);

    stat_path("the app's eboot.bin", "app0:eboot.bin", 0);
    stat_path("the app's sce_sys directory", "app0:sce_sys", 1);
    stat_path("app0:", "app0:", 1);
    stat_path("a file created with mode 0666", STAT_DIR "/file", 1);
    stat_path("a file created with mode 0", STAT_DIR "/file0", 1);
    stat_path("a file created with mode 0777", STAT_DIR "/file777", 1);
    stat_path("a directory created with mode 0777", STAT_DIR "/dir", 1);
    stat_path("a directory created with mode 0", STAT_DIR "/dir0", 1);
    stat_path("ux0:data", "ux0:data", 1);
    stat_path("ux0:", "ux0:", 1);

    SceIoStat stat;
    sceClibMemset(&stat, 0xCC, sizeof(stat));
    const SceUID fd = sceIoOpen(STAT_DIR "/file", SCE_O_RDONLY, 0);
    PRINT("stat a file by descriptor", sceIoGetstatByFd(fd, &stat));
    PRINT("its mode by descriptor", stat.st_mode);
    PRINT("its attributes by descriptor", stat.st_attr);
    sceIoClose(fd);

    test_exit();
    return 0;
}
