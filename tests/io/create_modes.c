// The st_mode that sceIoGetstat reports for files and directories created with modes other than
// 0666 and 0777, and which modes the create refuses. Each one is changed back to 0666 and removed
// at the end.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include "test.h"

#define MODES_DIR "ux0:data/vita-autotests/create_modes"

static const int MODES[] = {0444, 0222, 0400, 0004, 0100, 0600, 0060, 0006, 0555};
#define MODE_COUNT (int)(sizeof(MODES) / sizeof(MODES[0]))

static void path_for(char *path, const char *kind, int mode) {
    sceClibSnprintf(path, 128, MODES_DIR "/%s%04o", kind, mode);
}

// Changes the entry back to mode 0666 so it can be removed, and removes it
static int restore_and_remove(const char *path, int is_dir) {
    SceIoStat stat;
    sceClibMemset(&stat, 0, sizeof(stat));
    stat.st_mode = 0666;
    sceIoChstat(path, &stat, SCE_CST_MODE);
    return is_dir ? sceIoRmdir(path) : sceIoRemove(path);
}

static void show_mode(const char *label, int mode, const char *path) {
    SceIoStat stat;
    sceClibMemset(&stat, 0xCC, sizeof(stat));
    const int ret = sceIoGetstat(path, &stat);
    PRINTF(ret < 0 ? ret : (int)stat.st_mode, "%s with mode 0%o: its mode", label, mode);
}

int main(void) {
    char path[128];
    sceIoMkdir(MODES_DIR, 0777);
    for (int i = 0; i < MODE_COUNT; i++) {
        path_for(path, "file", MODES[i]);
        restore_and_remove(path, 0);
        path_for(path, "dir", MODES[i]);
        restore_and_remove(path, 1);
    }

    for (int i = 0; i < MODE_COUNT; i++) {
        path_for(path, "file", MODES[i]);
        const SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT, MODES[i]);
        PRINTF(test_created(fd), "create a file with mode 0%o", MODES[i]);
        if (fd >= 0)
            sceIoClose(fd);
        show_mode("a file", MODES[i], path);
    }
    for (int i = 0; i < MODE_COUNT; i++) {
        path_for(path, "dir", MODES[i]);
        PRINTF(sceIoMkdir(path, MODES[i]), "create a directory with mode 0%o", MODES[i]);
        show_mode("a directory", MODES[i], path);
    }

    // A read only file can still be opened to read
    path_for(path, "file", 0444);
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    PRINT("open the mode 0444 file to read", test_created(fd));
    if (fd >= 0)
        sceIoClose(fd);
    fd = sceIoOpen(path, SCE_O_WRONLY, 0);
    PRINT("open the mode 0444 file to write", test_created(fd));
    if (fd >= 0)
        sceIoClose(fd);

    // Changing the mode of an existing file
    path_for(path, "file", 0600);
    SceIoStat stat;
    sceClibMemset(&stat, 0, sizeof(stat));
    stat.st_mode = 0444;
    PRINT("change the mode 0600 file to 0444", sceIoChstat(path, &stat, SCE_CST_MODE));
    show_mode("the changed file", 0444, path);
    stat.st_mode = 0;
    PRINT("change it to mode 0", sceIoChstat(path, &stat, SCE_CST_MODE));
    show_mode("the changed file", 0, path);

    for (int i = 0; i < MODE_COUNT; i++) {
        path_for(path, "file", MODES[i]);
        const int file_ret = restore_and_remove(path, 0);
        path_for(path, "dir", MODES[i]);
        const int dir_ret = restore_and_remove(path, 1);
        PRINTF(file_ret, "remove the mode 0%o file", MODES[i]);
        PRINTF(dir_ret, "remove the mode 0%o directory", MODES[i]);
    }
    PRINT("remove the test directory", sceIoRmdir(MODES_DIR));

    test_exit();
    return 0;
}
