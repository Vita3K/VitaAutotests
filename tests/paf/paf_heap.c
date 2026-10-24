// The ScePafMisc heaps: the struct sizes, creating a heap on a memory block, allocating from it with
// alignment, resizing and freeing, the heap running out, and deleting it
#include <psp2/kernel/sysmem.h>

#include "paf.h"

#define HEAP_SIZE 0x10000

int main(void) {
    paf_load_or_skip();

    PRINT("the heap context size", sizeof(ScePafHeapContext));
    PRINT("the heap option size", sizeof(ScePafHeapOpt));

    const SceUID block = sceKernelAllocMemBlock("paf heap", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, HEAP_SIZE, NULL);
    PRINT("allocate the memory block", test_created(block));
    unsigned char *base = NULL;
    sceKernelGetMemBlockBase(block, (void **)&base);

    ScePafHeapContext heap;
    ScePafHeapOpt opt;
    sce_paf_memset(&heap, 0, sizeof(heap));
    sce_paf_memset(&opt, 0, sizeof(opt));
    scePafCreateHeap(&heap, base, HEAP_SIZE, "test heap", &opt);
    PRINT("the heap keeps the base", heap.membase == base);
    PRINT("the heap keeps the size", heap.size);
    PRINT("the heap has a vtable", heap.vtable != NULL);
    PRINT("the heap has a heap", heap.heap != NULL);
    PRINT("the heap keeps the name", sce_paf_strcmp(heap.name, "test heap") == 0);
    PRINT("it imported the memory", heap.is_import_membase);

    void *a = scePafMallocWithContext(&heap, 100);
    PRINT("malloc 100 returns memory", a != NULL);
    PRINT("it is inside the block", (unsigned char *)a >= base && (unsigned char *)a + 100 <= base + HEAP_SIZE);
    PRINT("it is 8 byte aligned", ((uintptr_t)a & 7) == 0);
    sce_paf_memset(a, 0x5A, 100);
    void *b = scePafMallocWithContext(&heap, 100);
    PRINT("a second one doesn't overlap", b != NULL && ((uintptr_t)b >= (uintptr_t)a + 100 || (uintptr_t)a >= (uintptr_t)b + 100));

    static const SceUInt32 aligns[] = { 4, 16, 64, 256, 4096 };
    for (unsigned int i = 0; i < sizeof(aligns) / sizeof(aligns[0]); i++) {
        void *p = scePafMallocAlignWithContext(&heap, aligns[i], 100);
        PRINTF(p != NULL, "align %u returns memory", (unsigned)aligns[i]);
        PRINTF(p && ((uintptr_t)p & (aligns[i] - 1)) == 0, "it is %u byte aligned", (unsigned)aligns[i]);
        scePafFreeWithContext(&heap, p);
    }

    // Resizing keeps the contents
    unsigned char *r = scePafReallocWithContext(&heap, a, 400);
    PRINT("realloc to 400 returns memory", r != NULL);
    PRINT("it keeps the contents", r && r[0] == 0x5A && r[99] == 0x5A);
    r = scePafReallocWithContext(&heap, r, 10);
    PRINT("realloc down to 10 keeps the contents", r && r[0] == 0x5A && r[9] == 0x5A);
    void *fresh = scePafReallocWithContext(&heap, NULL, 50);
    PRINT("realloc NULL returns memory", fresh != NULL);
    scePafFreeWithContext(&heap, fresh);
    scePafFreeWithContext(&heap, r);
    scePafFreeWithContext(&heap, b);
    PRINT("free NULL does nothing", (scePafFreeWithContext(&heap, NULL), 0));

    // The heap's memory is used again
    void *c = scePafMallocWithContext(&heap, 1000);
    scePafFreeWithContext(&heap, c);
    PRINT("malloc after a free of the same size returns the same memory", scePafMallocWithContext(&heap, 1000) == c);

    // Running out. A size of 2 GiB crashes a Vita, so it isn't tried.
    void *big = scePafMallocWithContext(&heap, HEAP_SIZE / 2);
    PRINT("malloc half the heap", big != NULL);
    PRINT("malloc the whole heap", scePafMallocWithContext(&heap, HEAP_SIZE) != NULL);
    PRINT("malloc past the heap", scePafMallocWithContext(&heap, HEAP_SIZE + 1) != NULL);
    PRINT("memalign past the heap", scePafMallocAlignWithContext(&heap, 16, HEAP_SIZE * 2) != NULL);
    scePafFreeWithContext(&heap, big);
    PRINT("malloc half the heap after freeing it", scePafMallocWithContext(&heap, HEAP_SIZE / 2) != NULL);

    scePafDeleteHeap(&heap);
    PRINT("the memory block is still there after deleting", sceKernelFindMemBlockByAddr(base, 1) == block);
    PRINT("free the memory block", sceKernelFreeMemBlock(block));

    test_exit();
    return 0;
}
