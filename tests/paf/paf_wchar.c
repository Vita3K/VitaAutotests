// The ScePafStdc wide character calls, on 16 bit characters: lengths, comparisons, searches as
// offsets, copying and appending, spans and the wmem calls
#include "paf.h"

// Returns where p points into base in characters, or -1 for NULL
static int offset(const paf_wchar *p, const paf_wchar *base) {
    return p ? (int)(p - base) : -1;
}

// Compares a wide string with the characters of an ASCII one
static int same(const paf_wchar *w, const char *s) {
    for (; *s; w++, s++)
        if (*w != (paf_wchar)*s)
            return 0;
    return *w == 0;
}

int main(void) {
    paf_load_or_skip();

    static const paf_wchar hello[] = u"hello world, hello";
    static const paf_wchar abc[] = u"abc", abd[] = u"abd", ab[] = u"ab", big_abc[] = u"ABC", big_abd[] = u"ABD";
    static const paf_wchar wide[] = { 0x3042, 0x3044, 0 };
    static const paf_wchar high[] = { 0xFF21, 0 };

    // Length
    PRINT("the length of hello", sce_paf_wcslen(hello));
    PRINT("the length of an empty string", sce_paf_wcslen(u""));
    PRINT("the length of 2 wide characters", sce_paf_wcslen(wide));
    PRINT("the length of hello up to 3", sce_paf_wcsnlen(hello, 3));
    PRINT("the length of hello up to 100", sce_paf_wcsnlen(hello, 100));
    PRINT("the length of hello up to 0", sce_paf_wcsnlen(hello, 0));

    // Comparing
    PRINT("compare abc with abd", sce_paf_wcscmp(abc, abd));
    PRINT("compare abd with abc", sce_paf_wcscmp(abd, abc));
    PRINT("compare abc with abc", sce_paf_wcscmp(abc, abc));
    PRINT("compare ab with abc", sce_paf_wcscmp(ab, abc));
    PRINT("compare a wide character with a low one", sce_paf_wcscmp(wide, abc));
    PRINT("compare the first 2 of abc and abd", sce_paf_wcsncmp(abc, abd, 2));
    PRINT("compare the first 3 of abc and abd", sce_paf_wcsncmp(abc, abd, 3));
    PRINT("compare 0 characters", sce_paf_wcsncmp(abc, abd, 0));
    PRINT("compare ABC with abc ignoring case", sce_paf_wcscasecmp(big_abc, abc));
    PRINT("compare ABC with abd ignoring case", sce_paf_wcscasecmp(big_abc, abd));
    PRINT("compare ABD with abc ignoring case", sce_paf_wcscasecmp(big_abd, abc));
    PRINT("compare the first 2 of ABC and abd ignoring case", sce_paf_wcsncasecmp(big_abc, abd, 2));
    PRINT("compare the first 3 of ABC and abd ignoring case", sce_paf_wcsncasecmp(big_abc, abd, 3));
    PRINT("compare a full width A with a ignoring case", sce_paf_wcscasecmp(high, abc));
    PRINT("compare bytes of wide characters", sce_paf_wmemcmp(abc, abd, 3));
    PRINT("compare 2 wide characters equal", sce_paf_wmemcmp(abc, abd, 2));
    PRINT("compare 0 wide characters", sce_paf_wmemcmp(abc, abd, 0));

    // Searching
    PRINT("find the first l", offset(sce_paf_wcschr(hello, 'l'), hello));
    PRINT("find the last l", offset(sce_paf_wcsrchr(hello, 'l'), hello));
    PRINT("find a missing character", offset(sce_paf_wcschr(hello, 'z'), hello));
    PRINT("find the terminator", offset(sce_paf_wcschr(hello, 0), hello));
    PRINT("find the last terminator", offset(sce_paf_wcsrchr(hello, 0), hello));
    PRINT("find a wide character", offset(sce_paf_wcschr(wide, 0x3044), wide));
    PRINT("find w with wmemchr", offset(sce_paf_wmemchr(hello, 'w', 18), hello));
    PRINT("find w with wmemchr past the length", offset(sce_paf_wmemchr(hello, 'w', 5), hello));
    PRINT("find the first of o and w", offset(sce_paf_wcspbrk(hello, u"ow"), hello));
    PRINT("find the first of z and q", offset(sce_paf_wcspbrk(hello, u"zq"), hello));

    // Spans
    PRINT("the span of hel", sce_paf_wcsspn(hello, u"hel"));
    PRINT("the span of nothing", sce_paf_wcsspn(hello, u"xyz"));
    PRINT("the span without o", sce_paf_wcscspn(hello, u"o"));
    PRINT("the span without z", sce_paf_wcscspn(hello, u"z"));
    PRINT("the span without an empty set", sce_paf_wcscspn(hello, u""));

    // Copying
    paf_wchar buf[16];
    sce_paf_wmemset(buf, 'x', 16);
    PRINT("copy abc returns the destination", offset(sce_paf_wcscpy(buf, abc), buf));
    PRINT("it copies the terminator", same(buf, "abc") && buf[4] == 'x');
    sce_paf_wmemset(buf, 'x', 16);
    PRINT("copy abc into 8 returns the destination", offset(sce_paf_wcsncpy(buf, abc, 8), buf));
    PRINT("it pads the rest with zeros", buf[3] == 0 && buf[7] == 0 && buf[8] == 'x');
    sce_paf_wmemset(buf, 'x', 16);
    sce_paf_wcsncpy(buf, u"abcdef", 3);
    PRINT("copy 3 of abcdef leaves no terminator", buf[2] == 'c' && buf[3] == 'x');
    PRINT("wmemset returns the destination", offset(sce_paf_wmemset(buf, 0x3042, 4), buf));
    PRINT("it stores the whole character", buf[0] == 0x3042 && buf[3] == 0x3042 && buf[4] == 'x');
    sce_paf_wmemset(buf, 'x', 16);
    PRINT("wmemcpy returns the destination", offset(sce_paf_wmemcpy(buf, hello, 8), buf));
    PRINT("it copies", sce_paf_wmemcmp(buf, hello, 8) == 0 && buf[8] == 'x');
    sce_paf_wmemcpy(buf, u"0123456789ABCDEF", 16);
    PRINT("wmemmove up returns the destination", offset(sce_paf_wmemmove(buf + 2, buf, 8), buf));
    PRINT("it keeps the source whole", sce_paf_wmemcmp(buf, u"0101234567ABCDEF", 16) == 0);
    sce_paf_wmemcpy(buf, u"0123456789ABCDEF", 16);
    sce_paf_wmemmove(buf, buf + 2, 8);
    PRINT("wmemmove down keeps the source whole", sce_paf_wmemcmp(buf, u"2345678989ABCDEF", 16) == 0);

    // Appending
    sce_paf_wmemset(buf, 0, 16);
    sce_paf_wcscpy(buf, ab);
    PRINT("append cd returns the destination", offset(sce_paf_wcscat(buf, u"cd"), buf));
    PRINT("it appends", same(buf, "abcd"));
    sce_paf_wcsncat(buf, u"efgh", 2);
    PRINT("append 2 of efgh", same(buf, "abcdef"));
    sce_paf_wcsncat(buf, u"gh", 8);
    PRINT("append gh with a length of 8", same(buf, "abcdefgh"));

    // Case
    PRINT("lower A", sce_paf_private_towlower('A'));
    PRINT("lower a", sce_paf_private_towlower('a'));
    PRINT("lower a full width A", sce_paf_private_towlower(0xFF21));
    PRINT("lower a Greek capital sigma", sce_paf_private_towlower(0x3A3));
    PRINT("lower a Latin capital with a grave", sce_paf_private_towlower(0xC0));
    PRINT("lower [", sce_paf_private_towlower('['));
    PRINT("lower 0", sce_paf_private_towlower(0));

    test_exit();
    return 0;
}
