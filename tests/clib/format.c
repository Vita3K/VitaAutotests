// sceClibSnprintf: flags, widths, precisions, length modifiers and conversions, what it returns, and
// how it truncates. Each line's label holds the text it formatted, and its value is what it returned.
#include <psp2/kernel/clib.h>

#include "test.h"

// Some formats here are invalid on purpose
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#pragma GCC diagnostic ignored "-Wformat-overflow"

static char out[64];

#define FORMAT(...) PRINTF(sceClibSnprintf(out, sizeof(out), __VA_ARGS__), "%s gives [%s]", #__VA_ARGS__, out)

int main(void) {
    // Integers
    FORMAT("%d", 0);
    FORMAT("%d", -1);
    FORMAT("%d", (int)0x80000000);
    FORMAT("%d", 0x7FFFFFFF);
    FORMAT("%i", -5);
    FORMAT("%u", 0xFFFFFFFFu);
    FORMAT("%x", 0xABCDEFu);
    FORMAT("%X", 0xABCDEFu);
    FORMAT("%o", 8u);
    FORMAT("%5d", 42);
    FORMAT("%-5d|", 42);
    FORMAT("%05d", 42);
    FORMAT("%05d", -42);
    FORMAT("%+d", 42);
    FORMAT("% d", 42);
    FORMAT("%#x", 255u);
    FORMAT("%#X", 255u);
    FORMAT("%#o", 8u);
    FORMAT("%#x", 0u);
    FORMAT("%.3d", 7);
    FORMAT("%.0d", 0);
    FORMAT("%8.3d", 7);
    FORMAT("%*d", 6, 42);
    FORMAT("%-*d|", 6, 42);
    FORMAT("%.*d", 4, 42);

    // Length modifiers
    FORMAT("%lld", -1LL);
    FORMAT("%lld", 0x7FFFFFFFFFFFFFFFLL);
    FORMAT("%llx", 0x123456789ABCDEFULL);
    FORMAT("%llu", 0xFFFFFFFFFFFFFFFFULL);
    FORMAT("%ld", -7L);
    FORMAT("%lx", 0xDEADBEEFUL);
    FORMAT("%hd", (short)-2);
    FORMAT("%hu", (unsigned short)65535);
    FORMAT("%hhd", (signed char)-3);
    FORMAT("%hhx", (unsigned char)0xAB);
    FORMAT("%zu", (SceSize)123);
    FORMAT("%jd", (long long)-9);

    // Characters and strings
    FORMAT("%c", 'A');
    FORMAT("%3c|", 'B');
    FORMAT("%s", "text");
    FORMAT("%.2s", "hello");
    FORMAT("%8s|", "right");
    FORMAT("%-8s|", "left");
    FORMAT("%s", "");
    FORMAT("100%%");

    // Floating point
    FORMAT("%f", 1.5);
    FORMAT("%.2f", 3.14159);
    FORMAT("%8.3f", -2.5);
    FORMAT("%e", 12345.678);
    FORMAT("%g", 0.0001);
    FORMAT("%g", 1e20);

    // Conversions that aren't standard
    FORMAT("%k");
    FORMAT("%b", 5);
    FORMAT("%");

    // Truncation
    sceClibMemset(out, 'x', sizeof(out));
    PRINT("format abcdef into 4 bytes", sceClibSnprintf(out, 4, "abcdef"));
    PRINT("it keeps 3 and terminates", sceClibMemcmp(out, "abc", 4) == 0);
    sceClibMemset(out, 'x', sizeof(out));
    PRINT("format into 1 byte", sceClibSnprintf(out, 1, "abc"));
    PRINT("it only terminates", out[0] == 0 && out[1] == 'x');
    sceClibMemset(out, 'x', sizeof(out));
    PRINT("format into 0 bytes", sceClibSnprintf(out, 0, "abc"));
    PRINT("it writes nothing", out[0] == 'x');

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    FORMAT("%s", (const char *)NULL);
    PRINT("format into a NULL buffer of 0 bytes", sceClibSnprintf(NULL, 0, "abc"));

    test_exit();
    return 0;
}
