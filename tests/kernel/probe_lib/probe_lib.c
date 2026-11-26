// A module that exports the library ProbeLib, for module_library_copies. The test never calls its
// function, it only reads which loaded copy the app's import of it points to.
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>

int probe_lib_function(void) {
    return 0x1234;
}

int module_start(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    // vita-elf-create needs a module to import something
    sceKernelGetThreadId();
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return SCE_KERNEL_STOP_SUCCESS;
}
