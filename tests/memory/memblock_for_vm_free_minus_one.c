// What sceKernelFreeMemBlockForVM does with the UID -1 to the space that blocks for code share.
// The first block for code the test makes is 1 MiB, freed with sceKernelFreeMemBlockForVM. Then
// the call is made with -1. Then a 16 MiB block and a 1 MiB block are made while both live, which
// the 16 MiB the blocks share allow only when the call gave back space it never held. The test
// prints the result of each call, and frees what it made. It stands alone, since a later test in
// the same process would find the space changed.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define MIB 0x100000

// vitasdk has a stub for it but no prototype
int sceKernelFreeMemBlockForVM(SceUID uid);

int main(void) {
    const SceUID first = sceKernelAllocMemBlockForVM("vm first", MIB);
    const int free_first = first >= 0 ? sceKernelFreeMemBlockForVM(first) : first;
    const int free_minus_one = sceKernelFreeMemBlockForVM(-1);
    const SceUID big = sceKernelAllocMemBlockForVM("vm big", 16 * MIB);
    const SceUID small = sceKernelAllocMemBlockForVM("vm small", MIB);
    if (big >= 0)
        sceKernelFreeMemBlockForVM(big);
    if (small >= 0)
        sceKernelFreeMemBlockForVM(small);

    PRINT("allocate 1 MiB", test_created(first));
    PRINT("free it", free_first);
    PRINT("free the UID -1", free_minus_one);
    PRINT("allocate 16 MiB", test_created(big));
    PRINT("allocate 1 MiB with the 16 MiB live", test_created(small));
    test_exit();
    return 0;
}
