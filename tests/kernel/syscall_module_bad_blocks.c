// The module manager's start, stop, load and start, and stop and unload syscalls with a NULL or
// unmapped argument block, an unmapped option and an unmapped status pointer. Unmapped means the
// first page of the address space. probe_module counts the runs of its start and stop functions.
//
// On a PS Vita TV on firmware 3.65 each of these gives 0x80022005. The call copies its block in
// first and then the option, so a bad block or option stops it before the module id is looked up.
// The status is written back after the call, so with an unmapped status pointer the start still
// runs and the call gives 0x80022005 afterwards.
#include <psp2/kernel/modulemgr.h>

#include "probe_module/probe_module.h"
#include "syscalls.h"
#include "test.h"

#define MODULE_PATH "app0:probe_module.suprx"
#define NO_MODULE 0x7FFFFFF1

static ModuleProbe probe;
static ModuleArgs args = { &probe };

int main(void) {
    const ModuleCallBlock *const unmapped = (const ModuleCallBlock *)0x10;
    int status = 0;
    const ModuleCallBlock bad_option = { 0, (const void *)0x10, &status };
    const ModuleCallBlock bad_status = { 0, NULL, (int *)0x10 };

    PRINT("start with no block", syscall_start_module(NO_MODULE, sizeof(args), &args, NULL));
    PRINT("start with an unmapped block", syscall_start_module(NO_MODULE, sizeof(args), &args, unmapped));
    PRINT("start with an unmapped option", syscall_start_module(NO_MODULE, sizeof(args), &args, &bad_option));
    PRINT("start an unknown module with an unmapped status", syscall_start_module(NO_MODULE, sizeof(args), &args, &bad_status));
    PRINT("stop with no block", syscall_stop_module(NO_MODULE, sizeof(args), &args, NULL));
    PRINT("stop with an unmapped block", syscall_stop_module(NO_MODULE, sizeof(args), &args, unmapped));
    PRINT("stop with an unmapped option", syscall_stop_module(NO_MODULE, sizeof(args), &args, &bad_option));
    PRINT("stop an unknown module with an unmapped status", syscall_stop_module(NO_MODULE, sizeof(args), &args, &bad_status));
    PRINT("load and start with no block", syscall_load_start_module(MODULE_PATH, sizeof(args), &args, NULL));
    PRINT("load and start with an unmapped block", syscall_load_start_module(MODULE_PATH, sizeof(args), &args, unmapped));
    PRINT("load and start with an unmapped option", syscall_load_start_module(MODULE_PATH, sizeof(args), &args, &bad_option));
    PRINT("load and start a missing file with an unmapped status", syscall_load_start_module("app0:missing.suprx", sizeof(args), &args, &bad_status));
    PRINT("stop and unload with no block", syscall_stop_unload_module(NO_MODULE, sizeof(args), &args, NULL));
    PRINT("stop and unload with an unmapped block", syscall_stop_unload_module(NO_MODULE, sizeof(args), &args, unmapped));
    PRINT("stop and unload with an unmapped option", syscall_stop_unload_module(NO_MODULE, sizeof(args), &args, &bad_option));
    PRINT("stop and unload an unknown module with an unmapped status", syscall_stop_unload_module(NO_MODULE, sizeof(args), &args, &bad_status));
    PRINT("runs of both functions so far", probe.runs);

    // A loaded module started and stopped with an unmapped status
    const SceUID module = sceKernelLoadModule(MODULE_PATH, 0, NULL);
    PRINT("load", test_created(module));
    PRINT("start it with an unmapped status", syscall_start_module(module, sizeof(args), &args, &bad_status));
    PRINT("runs after the start", probe.runs);
    PRINT("stop it with an unmapped status", syscall_stop_module(module, sizeof(args), &args, &bad_status));
    PRINT("runs after the stop", probe.runs);
    PRINT("unload it", sceKernelUnloadModule(module, 0, NULL));

    test_exit();
    return 0;
}
