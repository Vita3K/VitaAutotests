// The ScePafStdc format calls: conversions, widths and flags, what snprintf returns and writes when
// the output doesn't fit, and the wide swprintf
#include <stdarg.h>

#include "paf.h"

// Formats with sce_paf_snprintf into a buffer of the given size and prints the length it returns, and
// the text
#define SHOW(label, size, ...)                                                    \
    do {                                                                          \
        char out_[64];                                                            \
        sce_paf_memset(out_, 'x', sizeof(out_));                                  \
        out_[sizeof(out_) - 1] = 0;                                               \
        const int len_ = sce_paf_snprintf(out_, size, __VA_ARGS__);               \
        PRINTF(len_, "%s: length", label);                                        \
        PRINTF(0, "%s: text [%s]", label, size ? out_ : "");                      \
    } while (0)

static int vformat(char *dst, SceSize max, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int ret = sce_paf_vsnprintf(dst, max, fmt, args);
    va_end(args);
    return ret;
}

static int vformat_unbounded(char *dst, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int ret = sce_paf_vsprintf(dst, fmt, args);
    va_end(args);
    return ret;
}

int main(void) {
    paf_load_or_skip();

    SHOW("plain text", 64, "hello");
    SHOW("signed", 64, "%d %i", -42, 17);
    SHOW("unsigned", 64, "%u", 4000000000u);
    SHOW("hex", 64, "%x %X %#x", 0xBEEF, 0xBEEF, 255);
    SHOW("octal", 64, "%o", 8);
    SHOW("character", 64, "%c%c", 'o', 'k');
    SHOW("string", 64, "%s", "text");
    SHOW("percent", 64, "100%%");
    SHOW("width", 64, "[%5d] [%-5d] [%05d]", 42, 42, 42);
    SHOW("sign flags", 64, "[%+d] [% d]", 5, 5);
    SHOW("string width and precision", 64, "[%6s] [%-6s] [%.2s]", "abc", "abc", "abc");
    SHOW("star width", 64, "[%*d] [%-*d]", 4, 7, 4, 7);
    SHOW("long long", 64, "%lld %llx", -5000000000LL, 0x123456789ALL);
    SHOW("short and char", 64, "%hd %hhd", 70000, 300);
    SHOW("size", 64, "%zu", (size_t)12);
    SHOW("pointer", 64, "%p", (void *)0x81000000);
    SHOW("a NULL string", 64, "%s", (const char *)NULL);
    SHOW("float", 64, "%.2f", 3.14159);
    SHOW("float width", 64, "[%8.3f] [%-8.1f]", 2.5, 2.5);
    SHOW("exponent", 64, "%e", 12345.678);
    SHOW("general", 64, "%g %g", 0.0001, 1234567.0);
    SHOW("negative zero", 64, "%.1f", -0.0);
    SHOW("an unknown conversion", 64, "%y");
    SHOW("a lone percent", 64, "abc%");

    // Truncation
    SHOW("exact fit", 6, "hello");
    SHOW("one short", 5, "hello");
    SHOW("far short", 2, "hello");
    SHOW("size 1", 1, "hello");
    char out[8];
    sce_paf_memset(out, 'x', sizeof(out));
    PRINT("size 0 returns the length", sce_paf_snprintf(out, 0, "hello"));
    PRINT("it writes nothing", out[0] == 'x');
    sce_paf_memset(out, 'x', sizeof(out));
    PRINT("a NULL buffer with size 0", sce_paf_snprintf(NULL, 0, "hello %d", 12));

    // The others
    char buf[64];
    PRINT("sprintf returns the length", sce_paf_sprintf(buf, "%d-%s", 12, "ab"));
    PRINT("it writes the text", sce_paf_strcmp(buf, "12-ab") == 0);
    PRINT("vsnprintf returns the length", vformat(buf, 64, "%d+%d", 1, 2));
    PRINT("it writes the text", sce_paf_strcmp(buf, "1+2") == 0);
    PRINT("vsnprintf truncates", vformat(buf, 3, "%d+%d", 10, 20));
    PRINT("it writes the start", sce_paf_strcmp(buf, "10") == 0);
    PRINT("vsprintf returns the length", vformat_unbounded(buf, "%s!", "go"));
    PRINT("it writes the text", sce_paf_strcmp(buf, "go!") == 0);

    // Wide
    paf_wchar wide[16];
    sce_paf_wmemset(wide, 'x', 16);
    PRINT("swprintf returns the length", sce_paf_swprintf(wide, 16, u"%d-%d", 12, 34));
    PRINT("it writes the text", sce_paf_wcscmp(wide, u"12-34") == 0);
    sce_paf_wmemset(wide, 'x', 16);
    PRINT("swprintf truncates and returns", sce_paf_swprintf(wide, 3, u"%d", 12345));
    PRINT("it writes the start", wide[0] == '1' && wide[1] == '2' && wide[2] == 0);
    PRINT("swprintf with size 0", sce_paf_swprintf(wide, 0, u"%d", 12345));

    test_exit();
    return 0;
}
