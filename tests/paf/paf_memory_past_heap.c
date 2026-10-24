// Allocating more than the global heap holds, which is the 16 MiB the load asked for. A wrong
// implementation may crash on it, so it has a test of its own.
#include "paf.h"

int main(void) {
    paf_load_or_skip();

    PRINT("malloc past the heap", sce_paf_malloc(0x1100000) != NULL);
    PRINT("memalign past the heap", sce_paf_memalign(16, 0x1100000) != NULL);
    void *p = sce_paf_malloc(100);
    PRINT("malloc 100 after them", p != NULL);
    sce_paf_free(p);

    test_exit();
    return 0;
}
