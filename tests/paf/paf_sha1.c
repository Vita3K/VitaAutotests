// The ScePafMisc SHA-1 calls: the digest of known inputs, feeding the data in pieces, a context used
// again, and the results of bad arguments
#include "paf.h"

// Prints the 20 bytes of a digest as 5 words, each as it reads from the bytes
static void print_digest(const char *what, const unsigned char *d) {
    for (int i = 0; i < 5; i++)
        PRINTF((d[i * 4] << 24) | (d[i * 4 + 1] << 16) | (d[i * 4 + 2] << 8) | d[i * 4 + 3], "%s word %d", what, i);
}

// Hashes data in one update, and prints the results of the calls and the digest
static void hash(const char *what, const void *data, SceSize length) {
    ScePafSha1Context ctx;
    unsigned char digest[20];
    sce_paf_memset(&ctx, 0xAA, sizeof(ctx));
    sce_paf_memset(digest, 0xBB, sizeof(digest));
    PRINTF(scePafSha1Init(&ctx), "%s: init", what);
    PRINTF(scePafSha1Update(&ctx, data, length), "%s: update", what);
    PRINTF(scePafSha1Result(&ctx, digest), "%s: result", what);
    print_digest(what, digest);
}

int main(void) {
    paf_load_or_skip();

    PRINT("the context size", sizeof(ScePafSha1Context));
    hash("empty", "", 0);
    hash("abc", "abc", 3);
    hash("the quick fox", "The quick brown fox jumps over the lazy dog", 43);
    hash("55 bytes", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 55);
    hash("56 bytes", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 56);
    hash("64 bytes", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 64);

    // In pieces
    ScePafSha1Context ctx;
    unsigned char digest[20];
    scePafSha1Init(&ctx);
    scePafSha1Update(&ctx, "a", 1);
    scePafSha1Update(&ctx, "", 0);
    scePafSha1Update(&ctx, "bc", 2);
    scePafSha1Result(&ctx, digest);
    print_digest("abc in pieces", digest);

    // A long input, 1000 bytes of a pattern
    static unsigned char data[1000];
    for (int i = 0; i < 1000; i++)
        data[i] = (unsigned char)i;
    hash("1000 bytes", data, 1000);
    scePafSha1Init(&ctx);
    for (int i = 0; i < 1000; i += 7)
        scePafSha1Update(&ctx, data + i, 1000 - i < 7 ? 1000 - i : 7);
    scePafSha1Result(&ctx, digest);
    print_digest("1000 bytes in pieces of 7", digest);

    // The context after the result, and used again
    PRINT("result again", scePafSha1Result(&ctx, digest));
    print_digest("result again", digest);
    PRINT("update after the result", scePafSha1Update(&ctx, "abc", 3));
    scePafSha1Result(&ctx, digest);
    print_digest("update after the result", digest);
    scePafSha1Init(&ctx);
    scePafSha1Update(&ctx, "abc", 3);
    scePafSha1Init(&ctx);
    scePafSha1Result(&ctx, digest);
    print_digest("init after an update", digest);

    // Bad arguments go last
    PRINT("init NULL", scePafSha1Init(NULL));
    scePafSha1Init(&ctx);
    PRINT("update with NULL data and a length", scePafSha1Update(&ctx, NULL, 3));
    PRINT("update with NULL data and no length", scePafSha1Update(&ctx, NULL, 0));
    PRINT("update NULL", scePafSha1Update(NULL, "abc", 3));
    PRINT("result into NULL", scePafSha1Result(&ctx, NULL));
    PRINT("result NULL", scePafSha1Result(NULL, digest));

    test_exit();
    return 0;
}
