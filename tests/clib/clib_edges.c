// sceClib string and format calls at the edges of their sizes: formatting more than fits, more than
// a kilobyte, into sizes past INT_MAX, and lengths of 0 or 0xFFFFFFFF for the string calls
#include <psp2/kernel/clib.h>

#include "test.h"

#pragma GCC diagnostic ignored "-Wformat-truncation"

static char big[4200];

static int vformat(char *dst, SceSize size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int ret = sceClibVsnprintf(dst, size, fmt, args);
    va_end(args);
    return ret;
}

int main(void) {
    char buf[64];

    // Formatting more than fits
    sceClibMemset(buf, 'x', sizeof(buf));
    PRINT("format 100 characters into 16 bytes", sceClibSnprintf(buf, 16, "%100d", 7));
    PRINT("it keeps 15 and terminates", sceClibStrnlen(buf, 64) == 15 && buf[16] == 'x');
    sceClibMemset(buf, 'x', sizeof(buf));
    PRINT("vsnprintf 100 characters into 16 bytes", vformat(buf, 16, "%100d", 7));
    PRINT("it keeps 15 and terminates", sceClibStrnlen(buf, 64) == 15 && buf[16] == 'x');
    PRINT("format a string of 70 characters into 64 bytes", sceClibSnprintf(buf, sizeof(buf), "%s", "0123456789012345678901234567890123456789012345678901234567890123456789"));
    PRINT("it ends with the 63rd", buf[62] == '2' && buf[63] == 0);

    // More than a kilobyte, past any buffer it may format through
    static const int widths[] = { 255, 256, 511, 512, 1023, 1024, 1025, 2048, 4095 };
    for (unsigned int i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        sceClibMemset(big, 'x', sizeof(big));
        const int ret = sceClibSnprintf(big, sizeof(big), "%*d|", widths[i], 5);
        PRINTF(ret, "format a width of %d", widths[i]);
        PRINTF(sceClibStrnlen(big, sizeof(big)), "format a width of %d: its length", widths[i]);
        PRINTF(big[widths[i] - 1] == '5' && big[widths[i]] == '|', "format a width of %d: it ends right", widths[i]);
    }
    sceClibMemset(big, 'x', sizeof(big));
    PRINT("format a precision of 3000", sceClibSnprintf(big, sizeof(big), "%.3000d", 1));
    PRINT("its length", sceClibStrnlen(big, sizeof(big)));
    sceClibMemset(big, 'a', 3000);
    big[3000] = 0;
    static char copy[4200];
    PRINT("format a string of 3000 characters", sceClibSnprintf(copy, sizeof(copy), "<%s>", big));
    PRINT("its length", sceClibStrnlen(copy, sizeof(copy)));
    PRINT("format it into 10 bytes", sceClibSnprintf(copy, 10, "<%s>", big));

    // Sizes past INT_MAX, with a short output
    static const SceSize sizes[] = { 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu };
    for (unsigned int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        sceClibMemset(buf, 'x', sizeof(buf));
        PRINTF(sceClibSnprintf(buf, sizes[i], "abc%d", 1), "format into a size of 0x%X", sizes[i]);
        PRINTF(sceClibMemcmp(buf, "abc1", 5) == 0 && buf[5] == 'x', "format into a size of 0x%X: it writes the text and a terminator", sizes[i]);
    }

    // Lengths of 0
    sceClibMemset(buf, 'x', sizeof(buf));
    PRINT("strncpy of 0 returns the destination", sceClibStrncpy(buf, "abc", 0) == buf);
    PRINT("it writes nothing", buf[0] == 'x');
    sceClibStrncpy(buf, "ab", 8);
    PRINT("strncat of 0 returns the destination", sceClibStrncat(buf, "cd", 0) == buf);
    PRINT("it appends nothing", sceClibStrcmp(buf, "ab") == 0);
    PRINT("strnlen up to 0", sceClibStrnlen("abc", 0));
    PRINT("strncasecmp of 0", sceClibStrncasecmp("abc", "xyz", 0));
    PRINT("memchr of 0", sceClibMemchr("abc", 'a', 0) == NULL);
    PRINT("memset of 0 returns the destination", sceClibMemset(buf, 0, 0) == buf);
    PRINT("memmove of 0 returns the destination", sceClibMemmove(buf, buf + 1, 0) == buf);

    // Lengths of 0xFFFFFFFF on terminated strings
    PRINT("strnlen up to 0xFFFFFFFF", sceClibStrnlen("abcde", 0xFFFFFFFFu));
    PRINT("strncmp of equal strings up to 0xFFFFFFFF", sceClibStrncmp("same", "same", 0xFFFFFFFFu));
    PRINT("strncmp of abc and abd up to 0xFFFFFFFF", sceClibStrncmp("abc", "abd", 0xFFFFFFFFu));
    PRINT("strncasecmp of ABC and abc up to 0xFFFFFFFF", sceClibStrncasecmp("ABC", "abc", 0xFFFFFFFFu));
    PRINT("strncasecmp of a and B", sceClibStrncasecmp("a", "B", 1));
    PRINT("strncasecmp of high bytes 0xC0 and 0xE0", sceClibStrncasecmp("\xC0", "\xE0", 1));
    PRINT("strncasecmp of [ and a", sceClibStrncasecmp("[", "a", 1));
    sceClibMemset(buf, 'x', sizeof(buf));
    buf[0] = 0;
    sceClibStrncat(buf, "hi", 0xFFFFFFFFu);
    PRINT("strncat of hi up to 0xFFFFFFFF", sceClibStrcmp(buf, "hi") == 0);
    sceClibMemset(buf, 'x', sizeof(buf));
    sceClibStrncpy(buf, "hi", 8);
    PRINT("strncpy pads to its length", buf[2] == 0 && buf[7] == 0 && buf[8] == 'x');

    // Searches that can't match
    PRINT("strstr for a needle longer than the text", sceClibStrstr("abc", "abcd") == NULL);
    PRINT("strstr in an empty text", sceClibStrstr("", "a") == NULL);
    PRINT("strstr of an empty needle in an empty text", sceClibStrstr("", "") != NULL);
    PRINT("strrchr for a character past 0xFF", sceClibStrrchr("hello", 0x100 + 'l') != NULL);
    PRINT("strchr for -1 in 0xFF", sceClibStrchr("a\xFF", -1) != NULL);

    test_exit();
    return 0;
}
