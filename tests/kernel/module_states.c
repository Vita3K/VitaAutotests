// What each module call returns for a module in each state, and the state sceKernelGetModuleInfo
// reports after each call. The test's module is used as it is and in variants whose start function
// returns 1 (no resident), 2 (failed) or another value, or whose stop function returns 1 (failed) or
// another value. probe_module counts the runs of its start and stop functions.
#include <psp2/kernel/modulemgr.h>

#include "probe_module/probe_module.h"
#include "test.h"

static ModuleProbe probe;
static ModuleArgs args = { &probe };

// Prints the call's result, its status, the runs so far and the module's state, or the error of the
// info call when the module is gone
static void print_step(const char *what, SceUID module, int result, int status) {
    PRINTF(result, "%s", what);
    PRINTF(status, "%s: the status", what);
    PRINTF(probe.runs, "%s: runs of the functions", what);
    SceKernelModuleInfo info = { .size = sizeof(info) };
    const int error = sceKernelGetModuleInfo(module, &info);
    PRINTF(error < 0 ? error : (int)info.state, "%s: the module's state", what);
}

static int start(SceUID module, int *status) {
    *status = 0xDEAD;
    return sceKernelStartModule(module, sizeof(args), &args, 0, NULL, status);
}

static int stop(SceUID module, int *status) {
    *status = 0xDEAD;
    return sceKernelStopModule(module, sizeof(args), &args, 0, NULL, status);
}

static int stop_unload(SceUID module, int *status) {
    *status = 0xDEAD;
    return sceKernelStopUnloadModule(module, sizeof(args), &args, 0, NULL, status);
}

static void normal(void) {
    int result;
    int status = 0xDEAD;
    probe.runs = 0;
    const SceUID module = sceKernelLoadModule("app0:probe_module.suprx", 0, NULL);
    result = test_created(module);
    print_step("load", module, result, status);
    result = stop(module, &status);
    print_step("stop before a start", module, result, status);
    result = start(module, &status);
    print_step("start", module, result, status);
    result = start(module, &status);
    print_step("start again", module, result, status);
    result = sceKernelUnloadModule(module, 0, NULL);
    print_step("unload while started", module, result, 0);
    result = stop(module, &status);
    print_step("stop", module, result, status);
    result = stop(module, &status);
    print_step("stop again", module, result, status);
    result = start(module, &status);
    print_step("start after the stop", module, result, status);
    result = sceKernelUnloadModule(module, 0, NULL);
    print_step("unload", module, result, 0);
    result = sceKernelUnloadModule(module, 0, NULL);
    print_step("unload again", module, result, 0);

    probe.runs = 0;
    const SceUID loaded = sceKernelLoadModule("app0:probe_module.suprx", 0, NULL);
    result = test_created(loaded);
    print_step("load to unload at once", loaded, result, 0);
    result = sceKernelUnloadModule(loaded, 0, NULL);
    print_step("unload before a start", loaded, result, 0);

    // Two loads of the same file
    const SceUID first = sceKernelLoadModule("app0:probe_module.suprx", 0, NULL);
    const SceUID second = sceKernelLoadModule("app0:probe_module.suprx", 0, NULL);
    PRINT("load the same file twice: both work", first >= 0 && second >= 0);
    PRINT("load the same file twice: two modules", first != second);
    sceKernelUnloadModule(first, 0, NULL);
    sceKernelUnloadModule(second, 0, NULL);
}

static void start_result(const char *path, const char *name) {
    char what[64];
    int result;
    int status = 0xDEAD;
    probe.runs = 0;
    probe.module = 0;
    const SceUID module = sceKernelLoadStartModule(path, sizeof(args), &args, 0, NULL, &status);
    // The module's own UID, which the start function recorded, shows whether it is still there
    sceClibSnprintf(what, sizeof(what), "%s: load and start", name);
    print_step(what, probe.module, module < 0 ? module : 0, status);
    PRINTF(module < 0 || module == probe.module, "%s: load and start: the UID is the module's", name);
    if (module >= 0) {
        sceClibSnprintf(what, sizeof(what), "%s: stop and unload", name);
        result = stop_unload(module, &status);
        print_step(what, module, result, status);
    }

    probe.runs = 0;
    const SceUID loaded = sceKernelLoadModule(path, 0, NULL);
    sceClibSnprintf(what, sizeof(what), "%s: start", name);
    result = start(loaded, &status);
    print_step(what, loaded, result, status);
    sceClibSnprintf(what, sizeof(what), "%s: start again", name);
    result = start(loaded, &status);
    print_step(what, loaded, result, status);
    sceClibSnprintf(what, sizeof(what), "%s: unload", name);
    result = sceKernelUnloadModule(loaded, 0, NULL);
    print_step(what, loaded, result, 0);
    sceClibSnprintf(what, sizeof(what), "%s: stop and unload", name);
    result = stop_unload(loaded, &status);
    print_step(what, loaded, result, status);
}

static void stop_result(const char *path, const char *name) {
    char what[64];
    int result;
    int status = 0xDEAD;
    probe.runs = 0;
    const SceUID module = sceKernelLoadStartModule(path, sizeof(args), &args, 0, NULL, &status);
    PRINTF(test_created(module), "%s: load and start", name);
    sceClibSnprintf(what, sizeof(what), "%s: stop", name);
    result = stop(module, &status);
    print_step(what, module, result, status);
    sceClibSnprintf(what, sizeof(what), "%s: stop again", name);
    result = stop(module, &status);
    print_step(what, module, result, status);
    sceClibSnprintf(what, sizeof(what), "%s: stop and unload", name);
    result = stop_unload(module, &status);
    print_step(what, module, result, status);
    sceClibSnprintf(what, sizeof(what), "%s: unload", name);
    result = sceKernelUnloadModule(module, 0, NULL);
    print_step(what, module, result, 0);
}

int main(void) {
    normal();
    start_result("app0:probe_start_no_resident.suprx", "start returns 1");
    start_result("app0:probe_start_failed.suprx", "start returns 2");
    start_result("app0:probe_start_other.suprx", "start returns 0x1234");
    stop_result("app0:probe_stop_failed.suprx", "stop returns 1");
    stop_result("app0:probe_stop_other.suprx", "stop returns 0x1234");
    test_exit();
    return 0;
}
