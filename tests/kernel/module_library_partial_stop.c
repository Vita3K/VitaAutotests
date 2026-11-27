// A stop of a module whose first library can't be unregistered. probe_two_libs exports ProbeLibA
// and then ProbeLib. probe_liba_strong imports ProbeLibA strongly. The app imports ProbeLibA and
// ProbeLib weakly and reads their import stubs to see which loaded module each one jumps to.
// probe_lib counts the runs of its start and stop functions.
// Cases:
// - a start of probe_two_libs: result, status, start runs, state, imports
// - a stop of probe_two_libs while the strong importer is started: result, status, stop runs,
//   state, imports
// - a second stop of probe_two_libs after the strong importer is stopped and unloaded, then its
//   unload
// The refused library is the first one, so the refused stop unregisters nothing. A refused stop
// after a library that was unregistered leaves a module no stop can end, and the process exit then
// never finishes, so the test can't show that case.
//
// On a PS Vita TV on firmware 3.65 the stop fails with 0x8002D002 after the stop function ran, both
// libraries stay registered and linked, and the second stop runs the stop function again and
// succeeds.
#include <psp2/kernel/modulemgr.h>

#include "probe_lib/probe_lib.h"
#include "probe_lib/stub_link.h"
#include "test.h"

#define TWO_LIBS_PATH "app0:probe_two_libs.suprx"
#define STRONG_PATH "app0:probe_liba_strong.suprx"

static SceUID two_libs = -3;
static LibRuns runs;
static LibArgs args = { &runs };

static void print_link(const char *what, const char *import, SceUIntPtr stub) {
    const SceUID module = stub_target(stub);
    const char *to = module == 0  ? "no module"
        : module == STUB_NO_MODULE ? "an address in no loaded module"
        : module == STUB_OTHER_FORM ? "a stub of another form"
        : module == two_libs      ? "probe_two_libs"
                                  : "another module";
    PRINTF(module == STUB_OTHER_FORM ? -1 : 0, "%s: the app's %s import is linked to %s", what, import, to);
}

static void print_links(const char *what) {
    print_link(what, "ProbeLibA", (SceUIntPtr)probe_lib_a_function);
    print_link(what, "ProbeLib", (SceUIntPtr)probe_lib_function);
}

// Prints the call's result, its status, the runs so far and the module's state
static void print_step(const char *what, int result, int status) {
    PRINTF(result, "%s", what);
    PRINTF(status, "%s: the status", what);
    PRINTF(runs.starts, "%s: runs of the start function", what);
    PRINTF(runs.stops, "%s: runs of the stop function", what);
    PRINTF(module_state(two_libs), "%s: the module's state", what);
}

static int stop(int *status) {
    *status = 0xDEAD;
    return sceKernelStopModule(two_libs, sizeof(args), &args, 0, NULL, status);
}

int main(void) {
    int status = 0xDEAD;
    print_links("before any load");

    two_libs = sceKernelLoadModule(TWO_LIBS_PATH, 0, NULL);
    PRINT("load probe_two_libs", test_created(two_libs));
    const int started = sceKernelStartModule(two_libs, sizeof(args), &args, 0, NULL, &status);
    print_step("start probe_two_libs", started, status);
    print_links("probe_two_libs started");

    const SceUID strong = sceKernelLoadStartModule(STRONG_PATH, 0, NULL, 0, NULL, &status);
    PRINT("load and start the strong importer", test_created(strong));

    if (started == 0) {
        const int result = stop(&status);
        print_step("stop probe_two_libs", result, status);
        print_links("probe_two_libs stop tried");
    }

    if (strong >= 0)
        PRINT("stop and unload the strong importer", sceKernelStopUnloadModule(strong, 0, NULL, 0, NULL, &status));

    if (started == 0) {
        const int result = stop(&status);
        print_step("stop probe_two_libs again", result, status);
        print_links("probe_two_libs stop tried again");
        if (result == 0)
            PRINT("unload probe_two_libs", sceKernelUnloadModule(two_libs, 0, NULL));
    }

    test_exit();
    return 0;
}
