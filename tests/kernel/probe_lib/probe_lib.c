// A module that exports the library ProbeLib, for module_library_copies and the other module_library
// tests. Built with PROBE_LIB_A as probe_two_libs, it also exports ProbeLibA ahead of ProbeLib. The
// tests never call its functions. Its start and stop functions count their runs through a pointer in
// their arguments.
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>

#include "probe_lib.h"

int probe_lib_function(void) {
    return 0x1234;
}

#ifdef PROBE_LIB_A
int probe_lib_a_function(void) {
    return 0x5678;
}
#endif

static LibRuns *runs_of(SceSize args, void *argp) {
    if (args < sizeof(LibArgs) || !argp)
        return NULL;
    return ((LibArgs *)argp)->out;
}

int module_start(SceSize args, void *argp) {
    // vita-elf-create needs a module to import something
    sceKernelGetThreadId();
    LibRuns *runs = runs_of(args, argp);
    if (runs)
        runs->starts++;
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
    LibRuns *runs = runs_of(args, argp);
    if (runs)
        runs->stops++;
    return SCE_KERNEL_STOP_SUCCESS;
}
