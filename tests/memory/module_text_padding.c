// The bytes after the end of the app's code in the last page of its code block. The test finds the
// block behind a function with sceKernelGetMemBlockInfoByAddr, counts the bytes at its end that
// hold 0xB6, and prints whether the last byte holds 0xB6 and whether the run of them is shorter
// than a page. The block's size is rounded up to a page, so a build whose code ends on a page
// boundary has no padding to see.
#include <psp2/kernel/sysmem.h>

#include "test.h"

int main(void) {
    SceKernelMemBlockInfo info = { .size = sizeof(info) };
    const int result = sceKernelGetMemBlockInfoByAddr((void *)((SceUIntPtr)main & ~(SceUIntPtr)1), &info);
    const volatile unsigned char *end = (const volatile unsigned char *)info.mappedBase + info.mappedSize;
    unsigned run = 0;
    while (run < info.mappedSize && end[-1 - (int)run] == 0xB6)
        run++;

    PRINT("the info of the code block", result);
    PRINT("the block is a whole number of pages", (info.mappedSize & 0xFFF) == 0);
    PRINT("the last byte of the block holds 0xB6", end[-1] == 0xB6);
    PRINT("the run of 0xB6 bytes at its end is shorter than a page", run < 0x1000);
    test_exit();
    return 0;
}
