// A start of a module whose second library can't be registered. probe_two_libs exports ProbeLibA and
// then ProbeLib, at the same version and with the same NID and name as probe_lib's ProbeLib. The app
// imports ProbeLibA and ProbeLib weakly and reads their import stubs to see which loaded module each
// one jumps to. probe_lib counts the runs of its start and stop functions.
// Cases:
// - a start of probe_two_libs while probe_lib is started: result, status, start runs, state, imports
// - a second start of probe_two_libs after probe_lib is stopped and unloaded
// - the imports after probe_two_libs is unloaded
//
// On a PS Vita TV on firmware 3.65 the start fails with 0x8002D013 without running the start
// function, ProbeLibA stays registered and linked to probe_two_libs, the second start fails the same
// way on ProbeLibA, and after the unload the app's ProbeLibA import still jumps into the unloaded
// module.
#include <psp2/kernel/modulemgr.h>

#include "probe_lib/probe_lib.h"
#include "probe_lib/stub_link.h"
#include "test.h"

#define PLAIN_PATH "app0:probe_lib.suprx"
#define TWO_LIBS_PATH "app0:probe_two_libs.suprx"

static SceUID plain = -3, two_libs = -3;
static LibRuns runs;
static LibArgs args = { &runs };

static void print_link(const char *what, const char *import, SceUIntPtr stub) {
    const SceUID module = stub_target(stub);
    const char *to = module == 0  ? "no module"
        : module == STUB_NO_MODULE ? "an address in no loaded module"
        : module == STUB_OTHER_FORM ? "a stub of another form"
        : module == plain         ? "probe_lib"
        : module == two_libs      ? "probe_two_libs"
                                  : "another module";
    PRINTF(module == STUB_OTHER_FORM ? -1 : 0, "%s: the app's %s import is linked to %s", what, import, to);
}

static void print_links(const char *what) {
    print_link(what, "ProbeLibA", (SceUIntPtr)probe_lib_a_function);
    print_link(what, "ProbeLib", (SceUIntPtr)probe_lib_function);
}

// Prints the call's result, its status, the start runs so far and the module's state
static void print_step(const char *what, SceUID module, int result, int status) {
    PRINTF(result, "%s", what);
    PRINTF(status, "%s: the status", what);
    PRINTF(runs.starts, "%s: runs of the start function", what);
    PRINTF(module_state(module), "%s: the module's state", what);
}

static int start(SceUID module, int *status) {
    *status = 0xDEAD;
    return sceKernelStartModule(module, sizeof(args), &args, 0, NULL, status);
}

int main(void) {
    int status = 0xDEAD;
    print_links("before any load");

    plain = sceKernelLoadStartModule(PLAIN_PATH, 0, NULL, 0, NULL, &status);
    PRINT("load and start probe_lib", test_created(plain));
    print_links("probe_lib started");

    two_libs = sceKernelLoadModule(TWO_LIBS_PATH, 0, NULL);
    PRINT("load probe_two_libs", test_created(two_libs));
    const int started = start(two_libs, &status);
    print_step("start probe_two_libs", two_libs, started, status);
    print_links("probe_two_libs start tried");

    if (plain >= 0) {
        PRINT("stop and unload probe_lib", sceKernelStopUnloadModule(plain, 0, NULL, 0, NULL, &status));
        print_links("probe_lib unloaded");
    }

    const int again = start(two_libs, &status);
    print_step("start probe_two_libs again", two_libs, again, status);
    print_links("probe_two_libs start tried again");

    if (again == 0)
        PRINT("stop and unload probe_two_libs", sceKernelStopUnloadModule(two_libs, 0, NULL, 0, NULL, &status));
    else
        PRINT("unload probe_two_libs", sceKernelUnloadModule(two_libs, 0, NULL));
    // Only reads the stubs, which may still jump into the unloaded module
    print_links("probe_two_libs unloaded");

    test_exit();
    return 0;
}
