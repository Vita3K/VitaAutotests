// The CPU affinity mask in the thread parameter block a module exports for the thread its start
// function runs on. The modules are probe_module built with a start block of priority 0x60, stack
// size 0x8000 and a mask of 0x80000, outside the user cores, of 0x20000, one user core, or of 0.
// For each the test prints the load and start result and the start status, and for a module that
// started, its start thread's priority, which shows the block was read, and the mask that thread had
// at creation and while it ran, then stops and unloads it.
//
// On a PS Vita TV on firmware 3.65 the mask 0x80000 fails the load and start with 0x80028025
// before the start function runs, and the status comes back 0.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>

#include "probe_module/probe_module.h"
#include "test.h"

static void load_and_unload(const char *name) {
    char path[64];
    sceClibSnprintf(path, sizeof(path), "app0:%s.suprx", name);
    ModuleProbe probe = { 0 };
    ModuleArgs start = { &probe };
    int status = 0xDEAD;
    const SceUID module = sceKernelLoadStartModule(path, sizeof(start), &start, 0, NULL, &status);
    PRINTF(test_created(module), "%s: load and start", name);
    PRINTF(status, "%s: the start status", name);
    PRINTF(probe.runs, "%s: runs of its start function", name);
    if (probe.runs) {
        PRINTF(probe.priority, "%s: its start thread's priority", name);
        PRINTF(probe.init_affinity, "%s: its start thread's initial affinity mask", name);
        PRINTF(probe.affinity, "%s: its start thread's affinity mask", name);
    }
    if (module >= 0) {
        status = 0xDEAD;
        PRINTF(sceKernelStopUnloadModule(module, 0, NULL, 0, NULL, &status), "%s: stop and unload", name);
    }
}

int main(void) {
    load_and_unload("probe_start_affinity_out");
    load_and_unload("probe_start_affinity_in");
    load_and_unload("probe_start_affinity_zero");

    test_exit();
    return 0;
}
