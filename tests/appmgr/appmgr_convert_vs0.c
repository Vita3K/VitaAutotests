// sceAppMgrConvertVs0UserDrivePath: the size check and what it does with vs0 and other paths.
#include <psp2/appmgr.h>
#include <psp2/kernel/clib.h>

#include "test.h"

#define FILL 0xAA

// Large enough for the biggest size the test passes
static char out[0x1000];

// Converts src into out with size and prints the result
static int convert(const char *src, int size) {
    char in[0x40];
    sceClibStrncpy(in, src, sizeof(in));
    sceClibMemset(out, FILL, sizeof(out));
    return sceAppMgrConvertVs0UserDrivePath(in, out, size);
}

int main(void) {
    PRINT("size 0x3F", convert("ux0:data/x", 0x3F));
    PRINT("size 0", convert("ux0:data/x", 0));
    PRINT("size 0x40", convert("ux0:data/x", 0x40));
    PRINT("size 0x40 copy", sceClibStrncmp(out, "ux0:data/x", 11) == 0);
    PRINT("size 0x124", convert("ux0:data/x", 0x124));
    PRINT("size 0x124 copy", sceClibStrncmp(out, "ux0:data/x", 11) == 0);
    PRINT("rest of out is zero", out[11] == 0 && out[0x100] == 0);
    PRINT("size 0x1000", convert("ux0:data/x", 0x1000));
    PRINT("other device", convert("ux0:vs0:sys/external", 0x124));

    PRINT("vs0 sys external", convert("vs0:sys/external/x", 0x124));
    PRINT("vs0 sys external is a copy", sceClibStrncmp(out, "vs0:sys/external/x", 19) == 0);
    PRINT("vs0 sys external changed", sceClibStrncmp(out, "vs0:", 4) != 0);
    PRINT("vs0 sys external with slash", convert("vs0:/sys/external/x", 0x124));
    PRINT("vs0 sys external with slash is a copy", sceClibStrncmp(out, "vs0:/sys/external/x", 20) == 0);
    PRINT("vs0 data external", convert("vs0:data/external/x", 0x124));
    PRINT("vs0 data external is a copy", sceClibStrncmp(out, "vs0:data/external/x", 20) == 0);
    PRINT("vs0 data external with slash", convert("vs0:/data/external/x", 0x124));
    PRINT("vs0 other path", convert("vs0:sys/other", 0x124));
    PRINT("vs0 other path is a copy", sceClibStrncmp(out, "vs0:sys/other", 14) == 0);

    char in[] = "ux0:data/x";
    PRINT("NULL src", sceAppMgrConvertVs0UserDrivePath(NULL, out, 0x124));
    PRINT("NULL dst", sceAppMgrConvertVs0UserDrivePath(in, NULL, 0x124));

    test_exit();
    return 0;
}
