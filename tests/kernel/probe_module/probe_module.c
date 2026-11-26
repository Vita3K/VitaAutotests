// A module whose start and stop functions record the thread they run on, for module_start_thread,
// module_start_vfp, module_thread_params and module_states
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>

#include "probe_module.h"
#include "registers.h"
#include "threadmgr_extra.h"

// A libkernel export the vitasdk headers don't declare
int sceKernelGetModuleInfoByAddr(void *addr, SceKernelModuleInfo *info);

#ifdef THREAD_PARAMS
// The 20 byte thread parameter block of the variants that have one: size word 4, priority, stack
// size, a word left unused and the CPU affinity mask. vita-elf-create puts only this symbol's
// variable in the main export, and the build rewrites that entry's NID to the block's NID.
__attribute__((used)) const SceUInt32 module_sdk_version[5] = { THREAD_PARAMS };
#endif

static void record(SceSize args, void *argp) {
    if (args < sizeof(ModuleArgs) || !argp)
        return;
    ModuleProbe *probe = ((ModuleArgs *)argp)->out;
    volatile int local = 0;
    probe->thread = sceKernelGetThreadId();
    probe->process = sceKernelGetProcessId();
    probe->priority = sceKernelGetThreadCurrentPriority();
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(probe->thread, &info);
    probe->init_priority = info.initPriority;
    probe->stack_size = info.stackSize;
    probe->attr = info.attr;
    probe->frame = (SceUIntPtr)&local;
    probe->argp = (SceUIntPtr)argp;
    probe->args = args;
    probe->runs++;
    probe->fpscr = get_fpscr() & FPSCR_CONTROL;
    probe->vfp_setting = sceKernelGetCurrentThreadVfpException();
    probe->tpidrurw = get_tpidrurw();
    SceKernelModuleInfo module_info = { .size = sizeof(module_info) };
    if (sceKernelGetModuleInfoByAddr((void *)record, &module_info) == 0)
        probe->module = module_info.modid;
    // Values of its own, which the stop function and the caller shouldn't see
    set_fpscr(get_fpscr() & ~FPSCR_FLAGS);
    set_rmode(RMODE_MINUS);
    sceKernelChangeThreadVfpException(0, SCE_KERNEL_VFP_EXCP_DZC);
    set_tpidrurw(0x4444);
}

// The variants for module_states return other values
#ifndef START_RESULT
#define START_RESULT SCE_KERNEL_START_SUCCESS
#endif
#ifndef STOP_RESULT
#define STOP_RESULT SCE_KERNEL_STOP_SUCCESS
#endif

int module_start(SceSize args, void *argp) {
    record(args, argp);
    return START_RESULT;
}

int module_stop(SceSize args, void *argp) {
    record(args, argp);
    return STOP_RESULT;
}
