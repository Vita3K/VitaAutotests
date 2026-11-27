// The checks of the module manager's start and stop syscalls before they run the module: the flags
// in the argument block and the status pointer, called through the syscalls with the block
// libkernel builds. probe_module counts the runs of its start and stop functions.
//
// On a PS Vita TV on firmware 3.65 the module id is checked first (0x80024501), then the flags
// (0x8002000A for any start or stop flag), then the status pointer (0x80020005, not for a stop and
// unload). Load and start takes flag 0x10. A refused call runs no function and leaves the status 0.
#include <psp2/kernel/modulemgr.h>

#include "probe_module/probe_module.h"
#include "syscalls.h"
#include "test.h"

#define MODULE_PATH "app0:probe_module.suprx"

static ModuleProbe probe;
static ModuleArgs args = { &probe };

// Prints a call's result, then its status and the runs so far, read after the call returned
static void print_call(const char *what, int result, const int *status) {
    PRINT(what, result);
    PRINTF(*status, "%s: the status", what);
    PRINTF(probe.runs, "%s: runs of both functions", what);
}

int main(void) {
    int status = 0xDEAD;
    ModuleCallBlock block = { 0, NULL, &status };
    ModuleCallBlock flag_1 = { 0x1, NULL, &status };
    ModuleCallBlock flag_10 = { 0x10, NULL, &status };
    ModuleCallBlock no_status = { 0, NULL, NULL };

    const SceUID module = syscall_load_start_module(MODULE_PATH, sizeof(args), &args, &flag_10);
    PRINT("load and start with flags 0x10", test_created(module));
    PRINT("load and start with flags 0x10: the status", status);

    status = 0xDEAD;
    print_call("start the started module with flags 0x1", syscall_start_module(module, sizeof(args), &args, &flag_1), &status);
    print_call("start the started module with no status pointer", syscall_start_module(module, sizeof(args), &args, &no_status), &status);
    status = 0xDEAD;
    print_call("stop with flags 0x1", syscall_stop_module(module, sizeof(args), &args, &flag_1), &status);
    print_call("stop with no status pointer", syscall_stop_module(module, sizeof(args), &args, &no_status), &status);
    status = 0xDEAD;
    print_call("stop and unload with flags 0x1", syscall_stop_unload_module(module, sizeof(args), &args, &flag_1), &status);
    status = 0xDEAD;
    print_call("stop and unload with no status pointer", syscall_stop_unload_module(module, sizeof(args), &args, &no_status), &status);

    // A loaded module that isn't started
    const SceUID loaded = sceKernelLoadModule(MODULE_PATH, 0, NULL);
    PRINT("load", test_created(loaded));
    status = 0xDEAD;
    print_call("start with flags 0x1", syscall_start_module(loaded, sizeof(args), &args, &flag_1), &status);
    print_call("start with no status pointer", syscall_start_module(loaded, sizeof(args), &args, &no_status), &status);
    status = 0xDEAD;
    print_call("start", syscall_start_module(loaded, sizeof(args), &args, &block), &status);
    status = 0xDEAD;
    print_call("stop and unload", syscall_stop_unload_module(loaded, sizeof(args), &args, &block), &status);

    // A module id that names no module
    status = 0xDEAD;
    print_call("start an unknown module with flags 0x1", syscall_start_module(0x7FFFFFF1, sizeof(args), &args, &flag_1), &status);
    status = 0xDEAD;
    print_call("stop an unknown module with no status pointer", syscall_stop_module(0x7FFFFFF1, sizeof(args), &args, &no_status), &status);

    test_exit();
    return 0;
}
