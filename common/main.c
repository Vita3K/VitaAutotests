// Runs one test, named by the first argument, by the arg of the URI that launched the app, like
// psgm:play?titleid=VTST00000&arg=<name>, or by the first line of SELECTION_PATH. "list" prints the
// name of every test.
#include <psp2/appmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include "test.h"

#define SELECTION_PATH "ux0:data/vita-autotests/test.txt"

char test_output_path[128];
const char *test_name;

#define TEST(name) int test_##name(void);
#include "tests.inc"
#undef TEST

static const struct {
    const char *name;
    int (*run)(void);
} tests[] = {
#define TEST(name) { #name, test_##name },
#include "tests.inc"
#undef TEST
};

// Reads the test name from SELECTION_PATH into buf. Returns 0 if there is none.
static int read_selection(char *buf, int size) {
    const SceUID fd = sceIoOpen(SELECTION_PATH, SCE_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    int len = sceIoRead(fd, buf, size - 1);
    sceIoClose(fd);
    if (len < 0)
        len = 0;
    buf[len] = '\0';
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ') {
            buf[i] = '\0';
            break;
        }
    }
    return buf[0] != '\0';
}

// Removes every file in dir, the captures of an earlier run
static void empty_dir(const char *dir) {
    const SceUID fd = sceIoDopen(dir);
    if (fd < 0)
        return;
    SceIoDirent entry;
    while (sceIoDread(fd, &entry) > 0) {
        char path[192];
        sceClibSnprintf(path, sizeof(path), "%s/%s", dir, entry.d_name);
        sceIoRemove(path);
    }
    sceIoDclose(fd);
}

// Reads the arg of the launch URI into buf. Returns 0 if there is none.
static int read_launch_arg(char *buf, int size) {
    char param[1024] = { 0 };
    if (sceAppMgrGetAppParam(param) < 0)
        return 0;
    const char *value = sceClibStrstr(param, "&arg=");
    if (!value)
        return 0;
    value += sizeof("&arg=") - 1;
    int len = 0;
    while (len < size - 1 && value[len] != '\0' && value[len] != '&') {
        buf[len] = value[len];
        len++;
    }
    buf[len] = '\0';
    return len > 0;
}

int main(int argc, char *argv[]) {
    char selection[64];
    const char *name = NULL;
    if (argc > 1)
        name = argv[1];
    else if (read_launch_arg(selection, sizeof(selection)) || read_selection(selection, sizeof(selection)))
        name = selection;
    if (!name) {
        sceClibPrintf("no test given, pass its name as the first argument, the launch URI arg or in " SELECTION_PATH "\n");
        return 2;
    }

    const int count = sizeof(tests) / sizeof(tests[0]);
    if (sceClibStrcmp(name, "list") == 0) {
        for (int i = 0; i < count; i++)
            sceClibPrintf("%s\n", tests[i].name);
        return 0;
    }
    for (int i = 0; i < count; i++) {
        if (sceClibStrcmp(name, tests[i].name) == 0) {
            // Each run starts a new output file
            sceIoMkdir("ux0:data/vita-autotests", 0777);
            sceIoMkdir(TEST_OUTPUT_DIR, 0777);
            sceClibSnprintf(test_output_path, sizeof(test_output_path), TEST_OUTPUT_DIR "/%s.txt", tests[i].name);
            sceIoRemove(test_output_path);
            // And empties the directory its captures go to, see capture.h
            char capture_dir[128];
            sceClibSnprintf(capture_dir, sizeof(capture_dir), TEST_OUTPUT_DIR "/%s", tests[i].name);
            empty_dir(capture_dir);
            test_name = tests[i].name;
            return tests[i].run();
        }
    }
    sceClibPrintf("unknown test: %s\n", name);
    return 2;
}
