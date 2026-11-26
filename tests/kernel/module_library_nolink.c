// A library export with the NOLINK attribute 0x0004. probe_lib_nolink is probe_lib with ProbeLib's
// attribute set to 0x0005. probe_lib_importer imports ProbeLib weakly and exports ProbeImporter,
// which gives the test the address of its ProbeLib import stub. The app imports ProbeLib and
// ProbeImporter weakly. The test reads the import stubs to see which loaded module each one jumps to.
// Cases:
// - the app's ProbeLib import, unlinked when the app starts, after probe_lib_nolink starts
// - the importer's ProbeLib import when the importer starts after probe_lib_nolink
// - a start of plain probe_lib while probe_lib_nolink is started: result, status, start runs, state
// - a start of a second copy of probe_lib_nolink: result, status, state, and both imports after it
// - the imports after the importer and both copies are stopped and unloaded in turn
//
// On a PS Vita TV on firmware 3.65 the start of probe_lib_nolink links the app's waiting ProbeLib
// import to it, an importer started after it doesn't link to it, and the second copy's start links
// the importer's waiting import to that copy.
#include <psp2/kernel/modulemgr.h>

#include "probe_lib/probe_lib.h"
#include "probe_lib/probe_lib_importer.h"
#include "probe_lib/stub_link.h"
#include "test.h"

#define NOLINK_PATH "app0:probe_lib_nolink.suprx"
#define PLAIN_PATH "app0:probe_lib.suprx"
#define IMPORTER_PATH "app0:probe_lib_importer.suprx"

static SceUID copy_a = -3, copy_b = -3, plain = -3, importer = -3;
static LibRuns runs;
static LibArgs args = { &runs };

static const char *name_of(SceUID module) {
    return module == 0             ? "no module"
        : module == STUB_NO_MODULE ? "an address in no loaded module"
        : module == STUB_OTHER_FORM ? "a stub of another form"
        : module == copy_a         ? "NOLINK copy A"
        : module == copy_b         ? "NOLINK copy B"
        : module == plain          ? "plain probe_lib"
        : module == importer       ? "the importer"
                                   : "another module";
}

static void print_link(const char *what, const char *import, SceUIntPtr stub) {
    const SceUID module = stub_target(stub);
    PRINTF(module == STUB_OTHER_FORM ? -1 : 0, "%s: %s is linked to %s", what, import, name_of(module));
}

// Prints where the app's ProbeLib import and, while the importer is started, the importer's ProbeLib
// import jump to
static void print_links(const char *what) {
    print_link(what, "the app's ProbeLib import", (SceUIntPtr)probe_lib_function);
    if (importer < 0)
        return;
    const SceUID target = stub_target((SceUIntPtr)probe_importer_stub);
    if (target != importer) {
        PRINTF(-1, "%s: the app's ProbeImporter import is linked to %s", what, name_of(target));
        return;
    }
    print_link(what, "the importer's ProbeLib import", probe_importer_stub());
}

static int start(SceUID module, int *status) {
    *status = 0xDEAD;
    return sceKernelStartModule(module, sizeof(args), &args, 0, NULL, status);
}

int main(void) {
    int status = 0xDEAD;
    print_links("before any load");

    copy_a = sceKernelLoadModule(NOLINK_PATH, 0, NULL);
    PRINT("load NOLINK copy A", test_created(copy_a));
    const int started_a = start(copy_a, &status);
    PRINT("start NOLINK copy A", started_a);
    PRINT("start NOLINK copy A: the status", status);
    PRINT("start NOLINK copy A: its state", module_state(copy_a));
    print_links("NOLINK copy A started");

    importer = sceKernelLoadStartModule(IMPORTER_PATH, 0, NULL, 0, NULL, &status);
    PRINT("load and start the importer", test_created(importer));
    print_links("the importer started");

    plain = sceKernelLoadModule(PLAIN_PATH, 0, NULL);
    PRINT("load plain probe_lib", test_created(plain));
    runs.starts = 0;
    PRINT("start plain probe_lib", start(plain, &status));
    PRINT("start plain probe_lib: the status", status);
    PRINT("start plain probe_lib: runs of its start function", runs.starts);
    PRINT("start plain probe_lib: its state", module_state(plain));
    print_links("plain probe_lib start tried");
    PRINT("unload plain probe_lib", sceKernelUnloadModule(plain, 0, NULL));

    copy_b = sceKernelLoadModule(NOLINK_PATH, 0, NULL);
    PRINT("load NOLINK copy B", test_created(copy_b));
    const int started_b = start(copy_b, &status);
    PRINT("start NOLINK copy B", started_b);
    PRINT("start NOLINK copy B: the status", status);
    PRINT("start NOLINK copy B: its state", module_state(copy_b));
    print_links("NOLINK copy B started");

    if (importer >= 0) {
        PRINT("stop and unload the importer", sceKernelStopUnloadModule(importer, 0, NULL, 0, NULL, &status));
        importer = -3;
    }
    if (started_b == 0) {
        PRINT("stop and unload NOLINK copy B", sceKernelStopUnloadModule(copy_b, 0, NULL, 0, NULL, &status));
        print_links("NOLINK copy B unloaded");
    }
    if (started_a == 0) {
        PRINT("stop and unload NOLINK copy A", sceKernelStopUnloadModule(copy_a, 0, NULL, 0, NULL, &status));
        print_links("NOLINK copy A unloaded");
    }

    test_exit();
    return 0;
}
