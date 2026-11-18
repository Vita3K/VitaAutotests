// sceAppMgrAppDataMount: the mount names it gives for each id, the ids it refuses, and mounting an id twice.
#include <psp2/appmgr.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include "test.h"

#define FILL 0xAA

static char name[16];

static int is_zero(void) {
    for (int i = 0; i < 16; i++)
        if (name[i])
            return 0;
    return 1;
}

static int hex_digits(const char *s, int n) {
    for (int i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F')))
            return 0;
    return 1;
}

// Mounts id and prints the result and whether the name is expected, then unmounts it
static void fixed(const char *label, int id, const char *expected) {
    sceClibMemset(name, FILL, sizeof(name));
    PRINTF(sceAppMgrAppDataMount(id, name), "%s: mount", label);
    PRINTF(sceClibStrncmp(name, expected, 16) == 0, "%s: name is %s", label, expected);
    SceIoStat stat;
    PRINTF(sceIoGetstat(expected, &stat) >= 0, "%s: stat of the name works", label);
    PRINTF(sceAppMgrUmount(expected), "%s: unmount", label);
}

// Prints the result and the 16 byte buffer of an id that is refused
static void refused(const char *label, int id) {
    sceClibMemset(name, FILL, sizeof(name));
    PRINTF(sceAppMgrAppDataMount(id, name), "%s: mount", label);
    PRINTF(is_zero(), "%s: buffer is 16 zero bytes", label);
}

int main(void) {
    fixed("id 100", 100, "photo0:");
    fixed("id 105", 105, "music0:");
    fixed("id 109", 109, "video0:");

    // A random name, "ad", 12 hex digits and a colon
    sceClibMemset(name, FILL, sizeof(name));
    PRINT("id 101: mount", sceAppMgrAppDataMount(101, name));
    PRINT("id 101: name has 15 characters", sceClibStrnlen(name, 16) == 15);
    PRINT("id 101: name starts with ad", name[0] == 'a' && name[1] == 'd');
    PRINT("id 101: 12 hex digits", hex_digits(name + 2, 12));
    PRINT("id 101: name ends with a colon", name[14] == ':');
    PRINT("id 101: unmount", sceAppMgrUmount(name));

    refused("id 104", 104);
    refused("id 106", 106);
    refused("id 110", 110);
    refused("id 99", 99);
    refused("id 113", 113);
    refused("id 0", 0);
    PRINT("NULL out", sceAppMgrAppDataMount(100, NULL));

    // The same id twice gives the existing name
    sceClibMemset(name, FILL, sizeof(name));
    PRINT("first mount of 100", sceAppMgrAppDataMount(100, name));
    char first[16];
    sceClibMemcpy(first, name, 16);
    sceClibMemset(name, FILL, sizeof(name));
    PRINT("second mount of 100", sceAppMgrAppDataMount(100, name));
    PRINT("second mount gives the same name", sceClibStrncmp(name, first, 16) == 0);
    PRINT("unmount after both", sceAppMgrUmount(first));
    PRINT("unmount again", sceAppMgrUmount(first));

    test_exit();
    return 0;
}
