// sceAppMgrLoadExec: the argument checks that make it fail, so that it never replaces the test app.
#include <psp2/appmgr.h>
#include <psp2/kernel/clib.h>

#include "test.h"

#define BIG_SIZE 0x410

// Holds the long strings of the tests
static char big[BIG_SIZE];

int main(void) {
    // Unlike the other cases, a NULL path comes before the opt check
    PRINT("NULL path", sceAppMgrLoadExec(NULL, NULL, NULL));
    PRINT("NULL path with opt", sceAppMgrLoadExec(NULL, NULL, (SceAppMgrExecOptParam *)big));

    // opt must be NULL
    static char opt[0x100];
    PRINT("opt not NULL", sceAppMgrLoadExec("app0:eboot.bin", NULL, (SceAppMgrExecOptParam *)opt));

    // The path must be app0: followed by a name
    PRINT("subdirectory", sceAppMgrLoadExec("app0:sub/eboot.bin", NULL, NULL));
    PRINT("upper case device", sceAppMgrLoadExec("APP0:eboot.bin", NULL, NULL));
    PRINT("other device", sceAppMgrLoadExec("ux0:app/VTST00000/eboot.bin", NULL, NULL));
    PRINT("name with colon", sceAppMgrLoadExec("app0:a:b", NULL, NULL));
    PRINT("name with backslash inside", sceAppMgrLoadExec("app0:a\\b", NULL, NULL));
    PRINT("two slashes", sceAppMgrLoadExec("app0://eboot.bin", NULL, NULL));

    // The path is copied with at most 0x124 bytes, and a longer one has no NUL
    sceClibMemset(big, 'a', sizeof(big));
    sceClibMemcpy(big, "app0:", 5);
    big[sizeof(big) - 1] = '\0';
    PRINT("path of 0x124 bytes or more", sceAppMgrLoadExec(big, NULL, NULL));

    test_exit();
    return 0;
}
