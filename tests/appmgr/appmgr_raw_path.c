// sceAppMgrGetRawPath: the pfs drive name it gives for app0: and what it does with other paths and sizes.
#include <psp2/appmgr.h>
#include <psp2/kernel/clib.h>

#include "test.h"

#define FILL 0xAA

// Large enough for the biggest size the test passes
static char out[0x1000];

static int is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int main(void) {
    sceClibMemset(out, FILL, sizeof(out));
    char path[] = "app0:eboot.bin";
    PRINT("app0 file", sceAppMgrGetRawPath(path, out, 0x124));
    PRINT("length", sceClibStrnlen(out, sizeof(out)));
    PRINT("starts with PD", out[0] == 'P' && out[1] == 'D');
    int hex = 1;
    for (int i = 2; i < 14; i++)
        hex &= is_hex(out[i]);
    PRINT("12 hex digits after PD", hex);
    PRINT("suffix is d0:eboot.bin", sceClibStrncmp(out + 14, "d0:eboot.bin", 13) == 0);

    sceClibMemset(out, FILL, sizeof(out));
    char root[] = "app0:";
    PRINT("app0 root", sceAppMgrGetRawPath(root, out, 0x124));
    PRINT("root length", sceClibStrnlen(out, sizeof(out)));
    PRINT("root suffix is d0:", sceClibStrncmp(out + 14, "d0:", 4) == 0);

    sceClibMemset(out, FILL, sizeof(out));
    char other[] = "ux0:data/x";
    PRINT("ux0 path", sceAppMgrGetRawPath(other, out, 0x124));
    PRINT("ux0 path is unchanged", sceClibStrncmp(out, "ux0:data/x", 11) == 0);

    sceClibMemset(out, FILL, sizeof(out));
    char unknown[] = "nodev0:file";
    PRINT("unknown device", sceAppMgrGetRawPath(unknown, out, 0x124));
    PRINT("unknown device is unchanged", sceClibStrncmp(out, "nodev0:file", 12) == 0);

    sceClibMemset(out, FILL, sizeof(out));
    PRINT("size 5", sceAppMgrGetRawPath(path, out, 5));
    PRINT("size 0", sceAppMgrGetRawPath(path, out, 0));
    PRINT("size 0x1000", sceAppMgrGetRawPath(path, out, 0x1000));
    PRINT("length with size 0x1000 is at most 0x123", sceClibStrnlen(out, sizeof(out)) <= 0x123);

    // The path is copied with at most 0x124 bytes, so a longer one has no NUL
    static char longpath[0x130];
    sceClibMemset(longpath, 'a', sizeof(longpath));
    longpath[sizeof(longpath) - 1] = '\0';
    PRINT("path longer than 0x124", sceAppMgrGetRawPath(longpath, out, 0x124));

    test_exit();
    return 0;
}
