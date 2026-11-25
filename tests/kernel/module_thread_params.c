// The thread parameter blocks a module exports for the threads its start and stop functions run on:
// a block of size word 4 sets their priority and stack size, a block of another size changes
// nothing, and the start block and the stop block are separate. The modules are probe_module
// built with a block, each records the thread it runs on.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>

#include "probe_module/probe_module.h"
#include "test.h"

static void print_probe(const char *name, const char *what, const ModuleProbe *probe) {
    PRINTF(probe->runs, "%s: runs of its %s function", name, what);
    if (!probe->runs)
        return;
    PRINTF(probe->priority, "%s: its %s thread's priority", name, what);
    PRINTF(probe->init_priority, "%s: its %s thread's initial priority", name, what);
    PRINTF(probe->stack_size, "%s: its %s thread's stack size", name, what);
}

static void load_and_unload(const char *name) {
    char path[64];
    sceClibSnprintf(path, sizeof(path), "app0:%s.suprx", name);
    ModuleProbe start_probe = { 0 }, stop_probe = { 0 };
    ModuleArgs start = { &start_probe };
    ModuleArgs stop = { &stop_probe };
    int status = 0xDEAD;
    const SceUID module = sceKernelLoadStartModule(path, sizeof(start), &start, 0, NULL, &status);
    PRINTF(test_created(module), "%s: load and start", name);
    PRINTF(status, "%s: the start status", name);
    status = 0xDEAD;
    const int unloaded = module >= 0 ? sceKernelStopUnloadModule(module, sizeof(stop), &stop, 0, NULL, &status) : module;
    PRINTF(unloaded, "%s: stop and unload", name);
    PRINTF(status, "%s: the stop status", name);
    print_probe(name, "start", &start_probe);
    print_probe(name, "stop", &stop_probe);
}

int main(void) {
    // A start block of priority 0x60 and stack size 0x8000
    load_and_unload("probe_start_params");
    // A stop block of priority 0x50 and stack size 0x6000
    load_and_unload("probe_stop_params");
    // The same start block as the first, with size word 0x14
    load_and_unload("probe_start_params_size");

    test_exit();
    return 0;
}
