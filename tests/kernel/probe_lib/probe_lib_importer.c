// A module that imports a probe library and doesn't call it. Built with ProbeLib's weak stubs as
// probe_lib_importer, which exports ProbeImporter so the test can read its import stub, and with
// ProbeLibA's strong stubs as probe_liba_strong.
#include <psp2/kernel/modulemgr.h>

#include "probe_lib.h"
#include "probe_lib_importer.h"

SceUIntPtr probe_importer_stub(void) {
#ifdef PROBE_IMPORT_A
    return (SceUIntPtr)probe_lib_a_function;
#else
    return (SceUIntPtr)probe_lib_function;
#endif
}

int module_start(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return SCE_KERNEL_STOP_SUCCESS;
}
