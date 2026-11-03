// sceClibMemcpy, Memcpy_safe, Memmove, Memset, Memcmp and Memchr across sizes around word, cache line
// and page boundaries, at every alignment of their pointers. A copy must write exactly its bytes and
// leave the guard bytes around them. Each line holds a mask of the alignments that went wrong, so a
// right one prints 0. Memcpy_safe only gets ranges that don't overlap, since a Vita aborts the
// process when they do.
#include <psp2/kernel/clib.h>

#include "test.h"

#define GUARD 64
#define MAX_LEN 4100
#define BUF_SIZE (GUARD + 8 + MAX_LEN + GUARD)

static unsigned char src_buf[BUF_SIZE];
static unsigned char dst_buf[BUF_SIZE] __attribute__((aligned(64)));
static unsigned char ref_buf[BUF_SIZE];

static const int lens[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257, 1023, 1024, 1025, 4095, 4096, 4097 };
#define LEN_COUNT (int)(sizeof(lens) / sizeof(lens[0]))

static void fill_pattern(unsigned char *buf, int seed) {
    for (int i = 0; i < BUF_SIZE; i++)
        buf[i] = (unsigned char)(i * 7 + seed);
}

// The mask of destination alignments 0 to 7, for one source alignment, where a copy of len bytes went
// wrong: a wrong byte, a written guard byte, or a return value that isn't the destination
static int copy_mask(void *(*copy)(void *, const void *, SceSize), int src_align, int len) {
    int mask = 0;
    for (int dst_align = 0; dst_align < 8; dst_align++) {
        fill_pattern(src_buf, 1);
        fill_pattern(dst_buf, 99);
        unsigned char *dst = dst_buf + GUARD + dst_align;
        const unsigned char *src = src_buf + GUARD + src_align;
        void *ret = copy(dst, src, len);
        int bad = ret != dst;
        for (int i = 0; i < BUF_SIZE && !bad; i++) {
            const int in_copy = i >= GUARD + dst_align && i < GUARD + dst_align + len;
            const unsigned char want = in_copy ? src[i - GUARD - dst_align] : (unsigned char)(i * 7 + 99);
            bad = dst_buf[i] != want;
        }
        mask |= bad << dst_align;
    }
    return mask;
}

static int set_mask(int value, int len) {
    int mask = 0;
    for (int dst_align = 0; dst_align < 8; dst_align++) {
        fill_pattern(dst_buf, 99);
        unsigned char *dst = dst_buf + GUARD + dst_align;
        void *ret = sceClibMemset(dst, value, len);
        int bad = ret != dst;
        for (int i = 0; i < BUF_SIZE && !bad; i++) {
            const int in_set = i >= GUARD + dst_align && i < GUARD + dst_align + len;
            bad = dst_buf[i] != (in_set ? (unsigned char)value : (unsigned char)(i * 7 + 99));
        }
        mask |= bad << dst_align;
    }
    return mask;
}

// Moves len bytes by distance inside one buffer, and compares with a byte-by-byte move through a copy
static int move_mask(void *(*move)(void *, const void *, SceSize), int distance, int len) {
    int mask = 0;
    for (int align = 0; align < 8; align++) {
        fill_pattern(dst_buf, 5);
        fill_pattern(ref_buf, 5);
        unsigned char *from = dst_buf + GUARD + 8 + align;
        unsigned char *to = from + distance;
        unsigned char tmp[MAX_LEN];
        for (int i = 0; i < len; i++)
            tmp[i] = ref_buf[GUARD + 8 + align + i];
        for (int i = 0; i < len; i++)
            ref_buf[GUARD + 8 + align + distance + i] = tmp[i];
        void *ret = move(to, from, len);
        int bad = ret != to;
        for (int i = 0; i < BUF_SIZE && !bad; i++)
            bad = dst_buf[i] != ref_buf[i];
        mask |= bad << align;
    }
    return mask;
}

int main(void) {
    // Copies at every source alignment, each line a mask of destination alignments
    static const struct {
        const char *name;
        void *(*func)(void *, const void *, SceSize);
    } copies[] = {
        { "memcpy", sceClibMemcpy },
        { "memcpy_safe", sceClibMemcpy_safe },
        { "memmove", sceClibMemmove },
    };
    for (unsigned int c = 0; c < sizeof(copies) / sizeof(copies[0]); c++) {
        for (int l = 0; l < LEN_COUNT; l++) {
            int mask = 0;
            for (int src_align = 0; src_align < 8; src_align++)
                mask |= copy_mask(copies[c].func, src_align, lens[l]) << (src_align * 4 % 32);
            // Folds the 64 cases into 32 bits, which still shows if any of them went wrong
            PRINTF(mask, "%s of %d bytes: wrong alignments", copies[c].name, lens[l]);
        }
    }

    // Sets
    for (int l = 0; l < LEN_COUNT; l++)
        PRINTF(set_mask(0xA5, lens[l]), "memset of %d bytes: wrong alignments", lens[l]);
    PRINT("memset of 64 bytes with 0x1234: wrong alignments", set_mask(0x1234, 64));
    PRINT("memset of 64 bytes with -1: wrong alignments", set_mask(-1, 64));

    // Overlapping moves, by distances forward and back
    static const int distances[] = { -8, -7, -4, -3, -1, 1, 3, 4, 7, 8 };
    static const int move_lens[] = { 1, 7, 16, 33, 64, 257, 1024 };
    for (unsigned int d = 0; d < sizeof(distances) / sizeof(distances[0]); d++) {
        for (unsigned int l = 0; l < sizeof(move_lens) / sizeof(move_lens[0]); l++) {
            PRINTF(move_mask(sceClibMemmove, distances[d], move_lens[l]), "memmove of %d bytes by %d: wrong alignments", move_lens[l], distances[d]);
        }
    }
    // Memcpy_safe right next to its source, before and after it, where the ranges touch but don't overlap
    for (unsigned int l = 0; l < sizeof(move_lens) / sizeof(move_lens[0]); l++) {
        if (move_lens[l] > GUARD + 8)
            continue;
        PRINTF(move_mask(sceClibMemcpy_safe, move_lens[l], move_lens[l]), "memcpy_safe of %d bytes to right after them: wrong alignments", move_lens[l]);
        PRINTF(move_mask(sceClibMemcpy_safe, -move_lens[l], move_lens[l]), "memcpy_safe of %d bytes to right before them: wrong alignments", move_lens[l]);
    }

    // Comparing a difference at each position, so a word-at-a-time compare must find the byte
    for (int l = 0; l < LEN_COUNT; l++) {
        const int len = lens[l];
        if (len == 0)
            continue;
        int wrong = 0;
        for (int align = 0; align < 8; align++) {
            static const int positions[] = { 0, 1, 3, 4, 7, 8, 15, 31, 63, 64, 1023, 4096 };
            for (unsigned int p = 0; p < sizeof(positions) / sizeof(positions[0]); p++) {
                const int pos = positions[p] < len ? positions[p] : len - 1;
                fill_pattern(src_buf, 1);
                fill_pattern(dst_buf, 1);
                unsigned char *a = src_buf + GUARD + align, *b = dst_buf + GUARD + (7 - align);
                a[pos] = 0x40;
                if (pos + 1 < len)
                    a[pos + 1] = 0x40;
                sceClibMemcpy(b, a, len);
                b[pos] = 0x41;
                // A byte past the difference that would give the other sign if it counted
                if (pos + 1 < len)
                    b[pos + 1] = 0x3F;
                const int less = sceClibMemcmp(a, b, len);
                const int more = sceClibMemcmp(b, a, len);
                const int equal = sceClibMemcmp(a, b, pos);
                wrong |= !(less < 0 && more > 0 && equal == 0);
                // An unsigned byte compare: 0xFF against 0x00
                b[pos] = a[pos] = 0;
                b[pos] = 0xFF;
                wrong |= !(sceClibMemcmp(b, a, pos + 1) > 0);
            }
        }
        PRINTF(wrong, "memcmp of %d bytes: wrong at some alignment", len);
    }
    PRINT("memcmp of bytes 0x80 and 0x7F", sceClibMemcmp("\x80", "\x7F", 1) > 0);
    PRINT("memcmp of bytes 0x01 and 0x02", sceClibMemcmp("\x01", "\x02", 1));
    PRINT("memcmp of bytes 0x10 and 0x01", sceClibMemcmp("\x10", "\x01", 1));
    PRINT("memcmp of bytes 0x00 and 0xFF", sceClibMemcmp("\x00", "\xFF", 1));

    // Searching for a byte at each position, and past the length
    for (int l = 0; l < LEN_COUNT; l++) {
        const int len = lens[l];
        int wrong = 0;
        for (int align = 0; align < 8; align++) {
            unsigned char *buf = dst_buf + GUARD + align;
            sceClibMemset(dst_buf, 0x11, BUF_SIZE);
            // The byte right after the range must not be found
            buf[len] = 0xEE;
            wrong |= sceClibMemchr(buf, 0xEE, len) != NULL;
            for (int pos = 0; pos < len; pos += pos < 70 ? 1 : 61) {
                buf[pos] = 0xEE;
                wrong |= sceClibMemchr(buf, 0xEE, len) != buf + pos;
                // The search compares the low byte of ch
                wrong |= sceClibMemchr(buf, 0x1EE, len) != buf + pos;
                buf[pos] = 0x11;
            }
        }
        PRINTF(wrong, "memchr of %d bytes: wrong at some position", len);
    }
    sceClibMemset(dst_buf, 0, 16);
    PRINT("memchr finds byte 0 in zeros", sceClibMemchr(dst_buf, 0, 16) == dst_buf);
    PRINT("memchr for -1 finds byte 0xFF", sceClibMemchr("\x01\xFF", -1, 2) != NULL);

    test_exit();
    return 0;
}
