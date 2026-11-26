// The module manager's load, start and stop calls, called with the argument block libkernel builds:
// the flags, the option pointer and the status pointer in one block passed as the fourth argument.
// probe_module counts the runs of its start and stop functions.
#include <psp2/kernel/modulemgr.h>

#include "probe_module/probe_module.h"
#include "syscalls.h"
#include "test.h"

#define MODULE_PATH "app0:probe_module.suprx"

static ModuleProbe probe;

int main(void) {
    ModuleArgs args = { &probe };

    int status = 0xDEAD;
    ModuleCallBlock block = { 0, NULL, &status };
    const SceUID module = syscall_load_start_module(MODULE_PATH, sizeof(args), &args, &block);
    PRINT("load and start", test_created(module));
    PRINT("load and start: the status", status);
    PRINT("load and start: runs of the start function", probe.runs);

    status = 0xDEAD;
    PRINT("stop", syscall_stop_module(module, sizeof(args), &args, &block));
    PRINT("stop: the status", status);
    PRINT("stop: runs of both functions", probe.runs);

    status = 0xDEAD;
    PRINT("start again", syscall_start_module(module, sizeof(args), &args, &block));
    PRINT("start again: the status", status);
    PRINT("start again: runs of both functions", probe.runs);

    status = 0xDEAD;
    PRINT("stop and unload", syscall_stop_unload_module(module, sizeof(args), &args, &block));
    PRINT("stop and unload: the status", status);
    PRINT("stop and unload: runs of both functions", probe.runs);

    // A block with no status pointer, then one with flags the call doesn't take
    ModuleCallBlock no_status = { 0, NULL, NULL };
    const SceUID second = syscall_load_start_module(MODULE_PATH, sizeof(args), &args, &no_status);
    PRINT("load and start with no status pointer", test_created(second));
    PRINT("stop and unload with no status pointer", second >= 0 ? syscall_stop_unload_module(second, sizeof(args), &args, &no_status) : second);
    status = 0xDEAD;
    ModuleCallBlock bad_flags = { 0x1, NULL, &status };
    const SceUID third = syscall_load_start_module(MODULE_PATH, sizeof(args), &args, &bad_flags);
    PRINT("load and start with flags 0x1", test_created(third));
    PRINT("load and start with flags 0x1: the status", status);
    if (third >= 0)
        sceKernelStopUnloadModule(third, 0, NULL, 0, NULL, NULL);

    test_exit();
    return 0;
}
