// The ScePafStdc memory calls: comparing, searching, copying, filling and overlapping moves, and the
// global heap's allocations with their alignment, until it runs out
#include "paf.h"

// Returns where p points into base, or -1 for NULL
static int offset(const void *p, const void *base) {
    return p ? (int)((const char *)p - (const char *)base) : -1;
}

int main(void) {
    paf_load_or_skip();

    // Comparing
    PRINT("compare bytes 1 2 3 with 1 2 4", sce_paf_memcmp("\1\2\3", "\1\2\4", 3));
    PRINT("compare bytes 1 2 4 with 1 2 3", sce_paf_memcmp("\1\2\4", "\1\2\3", 3));
    PRINT("compare bytes 0xFF with 0x01", sce_paf_memcmp("\xFF", "\x01", 1));
    PRINT("compare equal bytes", sce_paf_memcmp("abc", "abc", 3));
    PRINT("compare 0 bytes", sce_paf_memcmp("a", "b", 0));
    PRINT("bcmp of different bytes", sce_paf_bcmp("\1\2\3", "\1\2\4", 3) != 0);
    PRINT("bcmp of equal bytes", sce_paf_bcmp("abc", "abc", 3));
    PRINT("bcmp of 0 bytes", sce_paf_bcmp("a", "b", 0));

    // Copying and filling
    unsigned char buf[16];
    sce_paf_memset(buf, 'x', sizeof(buf));
    PRINT("set returns the destination", offset(sce_paf_memset(buf, 0x1FF, 4), buf));
    PRINT("it stores the low byte", buf[0] == 0xFF && buf[3] == 0xFF && buf[4] == 'x');
    PRINT("set 0 bytes", offset(sce_paf_memset(buf, 0, 0), buf));
    PRINT("it stores nothing", buf[0] == 0xFF);
    PRINT("copy returns the destination", offset(sce_paf_memcpy(buf, "abcdefgh", 8), buf));
    PRINT("it copies", sce_paf_memcmp(buf, "abcdefgh", 8) == 0 && buf[8] == 'x');
    PRINT("copy 0 bytes", offset(sce_paf_memcpy(buf, "zzzz", 0), buf));
    PRINT("it copies nothing", buf[0] == 'a');
    sce_paf_memset(buf, 'x', sizeof(buf));
    sce_paf_bcopy("abcd", buf, 4);
    PRINT("bcopy takes the source first", sce_paf_memcmp(buf, "abcd", 4) == 0);
    sce_paf_bzero(buf, 3);
    PRINT("bzero clears 3 bytes", buf[0] == 0 && buf[2] == 0 && buf[3] == 'd');

    // Overlapping moves
    sce_paf_memcpy(buf, "0123456789ABCDEF", 16);
    PRINT("move up returns the destination", offset(sce_paf_memmove(buf + 2, buf, 8), buf));
    PRINT("it keeps the source whole", sce_paf_memcmp(buf, "0101234567ABCDEF", 16) == 0);
    sce_paf_memcpy(buf, "0123456789ABCDEF", 16);
    sce_paf_memmove(buf, buf + 2, 8);
    PRINT("move down keeps the source whole", sce_paf_memcmp(buf, "23456789" "89ABCDEF", 16) == 0);
    PRINT("move 0 bytes", offset(sce_paf_memmove(buf, buf + 1, 0), buf));
    sce_paf_memcpy(buf, "0123456789ABCDEF", 16);
    sce_paf_bcopy(buf, buf + 3, 8);
    PRINT("bcopy overlapping", sce_paf_memcmp(buf, "3456789A" "89ABCDEF", 16) == 0);

    // Allocating from the global heap
    void *a = sce_paf_malloc(100);
    PRINT("malloc 100 returns memory", a != NULL);
    PRINT("it is 8 byte aligned", ((uintptr_t)a & 7) == 0);
    sce_paf_memset(a, 0x5A, 100);
    PRINT("it can be written", ((unsigned char *)a)[99]);
    void *b = sce_paf_malloc(100);
    PRINT("a second one returns other memory", b != NULL && b != a);
    PRINT("the two don't overlap", (uintptr_t)b >= (uintptr_t)a + 100 || (uintptr_t)a >= (uintptr_t)b + 100);
    sce_paf_free(a);
    sce_paf_free(b);
    PRINT("free NULL does nothing", (sce_paf_free(NULL), 0));
    void *zero = sce_paf_malloc(0);
    PRINT("malloc 0 returns memory", zero != NULL);
    sce_paf_free(zero);

    static const SceSize aligns[] = { 4, 8, 16, 32, 64, 256, 4096, 0x10000 };
    for (unsigned int i = 0; i < sizeof(aligns) / sizeof(aligns[0]); i++) {
        void *p = sce_paf_memalign(aligns[i], 100);
        PRINTF(p != NULL, "memalign %u returns memory", (unsigned)aligns[i]);
        PRINTF(p && ((uintptr_t)p & (aligns[i] - 1)) == 0, "it is %u byte aligned", (unsigned)aligns[i]);
        sce_paf_free(p);
    }
    void *odd = sce_paf_memalign(24, 100);
    PRINT("memalign 24 returns memory", odd != NULL);
    sce_paf_free(odd);
    void *align0 = sce_paf_memalign(0, 100);
    PRINT("memalign 0 returns memory", align0 != NULL);
    sce_paf_free(align0);
    void *len0 = sce_paf_memalign(16, 0);
    PRINT("memalign with a length of 0 returns memory", len0 != NULL);
    sce_paf_free(len0);

    // A freed block is used again
    void *c = sce_paf_malloc(1000);
    sce_paf_free(c);
    void *d = sce_paf_malloc(1000);
    PRINT("malloc after a free of the same size returns the same memory", c == d);
    sce_paf_free(d);

    // Running out of memory. The cases that crash a Vita have tests of their own. The global heap is the 16 MiB the load asked for.
    void *big = sce_paf_malloc(0x800000);
    PRINT("malloc 8 MiB", big != NULL);
    void *big2 = sce_paf_malloc(0x800000);
    PRINT("malloc another 8 MiB", big2 != NULL);
    sce_paf_free(big);
    sce_paf_free(big2);
    void *again = sce_paf_malloc(0x800000);
    PRINT("malloc 8 MiB after freeing them", again != NULL);
    sce_paf_free(again);

    test_exit();
    return 0;
}
