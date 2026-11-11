// The bytes after the end of the app's code in the last page of its code block. The code's end is
// the module's first segment, from sceKernelGetModuleInfoByAddr, and the block's end comes from
// sceKernelGetMemBlockInfoByAddr. The test prints whether the block is a whole number of pages,
// whether it ends after the code, and whether every byte between the two holds 0xB6. A build whose
// code ends on a page boundary has no such bytes, and the test is skipped then.
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

int sceKernelGetModuleInfoByAddr(const void *addr, SceKernelModuleInfo *info);

int main(void) {
    const void *code = (const void *)((SceUIntPtr)main & ~(SceUIntPtr)1);
    SceKernelModuleInfo module = { .size = sizeof(module) };
    const int module_result = sceKernelGetModuleInfoByAddr(code, &module);
    SceKernelMemBlockInfo info = { .size = sizeof(info) };
    const int block_result = sceKernelGetMemBlockInfoByAddr((void *)code, &info);
    const SceUIntPtr code_end = (SceUIntPtr)module.segments[0].vaddr + module.segments[0].memsz;
    const SceUIntPtr block_end = (SceUIntPtr)info.mappedBase + info.mappedSize;
    if (module_result == 0 && block_result == 0 && code_end == block_end)
        test_skip("the code ends on a page boundary, so there are no bytes after it");

    int all_b6 = code_end < block_end;
    for (const volatile unsigned char *p = (const volatile unsigned char *)code_end; all_b6 && (SceUIntPtr)p < block_end; p++)
        all_b6 = *p == 0xB6;

    PRINT("the info of the module", module_result);
    PRINT("the info of the code block", block_result);
    PRINT("the block is a whole number of pages", (info.mappedSize & 0xFFF) == 0);
    PRINT("the block ends after the code", code_end < block_end);
    PRINT("every byte after the code holds 0xB6", all_b6);
    test_exit();
    return 0;
}
