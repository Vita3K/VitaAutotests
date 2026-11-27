// Reads an import stub to find the module whose code it jumps to, for the module_library tests
#pragma once

#include <psp2/kernel/modulemgr.h>

// The first word of an import stub that isn't linked to any library
#define STUB_UNLINKED 0xE24FC008

// What stub_target returns for a stub that jumps to an address in no loaded module, and for a stub
// of another form
#define STUB_NO_MODULE (-2)
#define STUB_OTHER_FORM (-1)

// A libkernel export the vitasdk headers don't declare
int sceKernelGetModuleInfoByAddr(void *addr, SceKernelModuleInfo *info);

// The module whose code the stub at stub_addr jumps to, or 0 for an unlinked stub
static inline SceUID stub_target(SceUIntPtr stub_addr) {
    const volatile SceUInt32 *const stub = (const volatile SceUInt32 *)(stub_addr & ~1u);
    if (stub[0] == STUB_UNLINKED)
        return 0;
    // movw ip, #lo and movt ip, #hi, then bx ip
    if ((stub[0] & 0xFFF0F000) != 0xE300C000 || (stub[1] & 0xFFF0F000) != 0xE340C000 || stub[2] != 0xE12FFF1C)
        return STUB_OTHER_FORM;
    const SceUInt32 lo = ((stub[0] >> 4) & 0xF000) | (stub[0] & 0xFFF);
    const SceUInt32 hi = ((stub[1] >> 4) & 0xF000) | (stub[1] & 0xFFF);
    SceKernelModuleInfo info = { .size = sizeof(info) };
    if (sceKernelGetModuleInfoByAddr((void *)(SceUIntPtr)((hi << 16) | lo), &info) < 0)
        return STUB_NO_MODULE;
    return info.modid;
}

// The state of a module as sceKernelGetModuleInfo reports it, or the error of the call
static inline int module_state(SceUID module) {
    SceKernelModuleInfo info = { .size = sizeof(info) };
    const int error = sceKernelGetModuleInfo(module, &info);
    return error < 0 ? error : (int)info.state;
}
