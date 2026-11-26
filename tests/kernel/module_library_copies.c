// Copies of a module that exports a library, loaded together. probe_lib exports the library ProbeLib
// and probe_lib_v2 is the same module with the library at version 2. The app imports ProbeLib
// weakly and never calls it: the test reads the app's import stub to see whether it is linked and
// which loaded module holds the code it jumps to.
#include <psp2/kernel/modulemgr.h>

#include "probe_lib/probe_lib.h"
#include "test.h"

#define LIB_PATH "app0:probe_lib.suprx"
#define LIB_V2_PATH "app0:probe_lib_v2.suprx"

// The first word of an import stub that isn't linked to any library
#define STUB_UNLINKED 0xE24FC008

// A libkernel export the vitasdk headers don't declare
int sceKernelGetModuleInfoByAddr(void *addr, SceKernelModuleInfo *info);

// The module whose code the import stub jumps to, 0 for an unlinked stub, -1 for another form
static SceUID linked_module(void) {
    const volatile SceUInt32 *const stub = (const volatile SceUInt32 *)((SceUIntPtr)probe_lib_function & ~1u);
    if (stub[0] == STUB_UNLINKED)
        return 0;
    // movw ip, #lo and movt ip, #hi, then bx ip
    if ((stub[0] & 0xFFF0F000) != 0xE300C000 || (stub[1] & 0xFFF0F000) != 0xE340C000)
        return -1;
    const SceUInt32 lo = ((stub[0] >> 4) & 0xF000) | (stub[0] & 0xFFF);
    const SceUInt32 hi = ((stub[1] >> 4) & 0xF000) | (stub[1] & 0xFFF);
    SceKernelModuleInfo info = { .size = sizeof(info) };
    if (sceKernelGetModuleInfoByAddr((void *)(SceUIntPtr)((hi << 16) | lo), &info) < 0)
        return -1;
    return info.modid;
}

static SceUID copy_a = -2, copy_b = -2, copy_v2 = -2, copy_again = -2;

// Prints which of the copies the import is linked to
static void print_link(const char *what) {
    const SceUID module = linked_module();
    const char *to = module == 0 ? "no copy"
        : module == copy_a       ? "copy A"
        : module == copy_b       ? "copy B"
        : module == copy_v2      ? "the v2 copy"
        : module == copy_again   ? "the copy loaded again"
                                 : "another module";
    PRINTF(module == -1 ? -1 : 0, "%s: the import is linked to %s", what, to);
}

static int state_of(SceUID module) {
    SceKernelModuleInfo info = { .size = sizeof(info) };
    const int error = sceKernelGetModuleInfo(module, &info);
    return error < 0 ? error : (int)info.state;
}

int main(void) {
    int status = 0;
    print_link("before any load");

    const SceUID a = copy_a = sceKernelLoadStartModule(LIB_PATH, 0, NULL, 0, NULL, &status);
    PRINT("load and start copy A", test_created(a));
    print_link("copy A started");

    const SceUID b = copy_b = sceKernelLoadModule(LIB_PATH, 0, NULL);
    PRINT("load copy B of the same file", test_created(b));
    PRINT("copy B is another module", b != a);
    status = 0xDEAD;
    PRINT("start copy B", sceKernelStartModule(b, 0, NULL, 0, NULL, &status));
    PRINT("start copy B: the status", status);
    PRINT("start copy B: its state", state_of(b));
    print_link("copy B start tried");
    PRINT("unload copy B", sceKernelUnloadModule(b, 0, NULL));
    print_link("copy B unloaded");

    const SceUID v2 = copy_v2 = sceKernelLoadStartModule(LIB_V2_PATH, 0, NULL, 0, NULL, &status);
    PRINT("load and start the v2 copy", test_created(v2));
    print_link("the v2 copy started");
    if (v2 >= 0) {
        PRINT("stop and unload the v2 copy", sceKernelStopUnloadModule(v2, 0, NULL, 0, NULL, &status));
        print_link("the v2 copy unloaded");
    }

    PRINT("stop and unload copy A", sceKernelStopUnloadModule(a, 0, NULL, 0, NULL, &status));
    print_link("copy A unloaded");

    const SceUID again = copy_again = sceKernelLoadStartModule(LIB_PATH, 0, NULL, 0, NULL, &status);
    PRINT("load and start the file again", test_created(again));
    print_link("loaded again");
    if (again >= 0)
        sceKernelStopUnloadModule(again, 0, NULL, 0, NULL, &status);
    print_link("unloaded again");

    test_exit();
    return 0;
}
