// The sceClib string and memory calls: results of comparisons, searches as offsets, copying, padding
// and overlapping moves, and the character tables
#include <psp2/kernel/clib.h>

#include "test.h"

// Returns where p points into base, or -1 for NULL
static int offset(const void *p, const void *base) {
    return p ? (int)((const char *)p - (const char *)base) : -1;
}

int main(void) {
    // Comparing
    PRINT("compare abc with abd", sceClibStrcmp("abc", "abd"));
    PRINT("compare abd with abc", sceClibStrcmp("abd", "abc"));
    PRINT("compare abc with abc", sceClibStrcmp("abc", "abc"));
    PRINT("compare ab with abc", sceClibStrcmp("ab", "abc"));
    PRINT("compare a high byte with a low one", sceClibStrcmp("\xC0", "A"));
    PRINT("compare the first 2 of abc and abd", sceClibStrncmp("abc", "abd", 2));
    PRINT("compare 0 characters", sceClibStrncmp("abc", "xyz", 0));
    PRINT("compare ABC with abd ignoring case", sceClibStrncasecmp("ABC", "abd", 3));
    PRINT("compare ABC with abc ignoring case", sceClibStrncasecmp("ABC", "abc", 3));
    PRINT("compare bytes 1 2 3 with 1 2 4", sceClibMemcmp("\1\2\3", "\1\2\4", 3));
    PRINT("compare bytes 0xFF with 0x01", sceClibMemcmp("\xFF", "\x01", 1));
    PRINT("compare 0 bytes", sceClibMemcmp("a", "b", 0));

    // Searching
    const char *text = "hello world, hello";
    PRINT("find the first l", offset(sceClibStrchr(text, 'l'), text));
    PRINT("find the last l", offset(sceClibStrrchr(text, 'l'), text));
    PRINT("find a missing character", offset(sceClibStrchr(text, 'z'), text));
    PRINT("find the terminator", offset(sceClibStrchr(text, '\0'), text));
    PRINT("find the last terminator", offset(sceClibStrrchr(text, '\0'), text));
    PRINT("find a character past 0xFF", offset(sceClibStrchr(text, 0x100 + 'w'), text));
    PRINT("find hello", offset(sceClibStrstr(text, "hello"), text));
    PRINT("find world", offset(sceClibStrstr(text, "world"), text));
    PRINT("find a missing string", offset(sceClibStrstr(text, "planet"), text));
    PRINT("find an empty string", offset(sceClibStrstr(text, ""), text));
    PRINT("find a byte", offset(sceClibMemchr(text, 'w', 18), text));
    PRINT("find a byte past the length", offset(sceClibMemchr(text, 'w', 5), text));
    PRINT("the length of hello up to 10", sceClibStrnlen("hello", 10));
    PRINT("the length of hello up to 3", sceClibStrnlen("hello", 3));
    PRINT("the length of an empty string", sceClibStrnlen("", 10));

    // Copying
    char buf[16];
    sceClibMemset(buf, 'x', sizeof(buf));
    PRINT("copy abc into 8 bytes returns the destination", offset(sceClibStrncpy(buf, "abc", 8), buf));
    PRINT("it pads the rest with zeros", buf[3] == 0 && buf[7] == 0 && buf[8] == 'x');
    sceClibMemset(buf, 'x', sizeof(buf));
    sceClibStrncpy(buf, "abcdef", 3);
    PRINT("copy 3 of abcdef leaves no terminator", buf[2] == 'c' && buf[3] == 'x');
    sceClibMemset(buf, 0, sizeof(buf));
    sceClibStrncpy(buf, "ab", 16);
    PRINT("append cd returns the destination", offset(sceClibStrncat(buf, "cd", 16), buf));
    PRINT("it appends", sceClibStrcmp(buf, "abcd") == 0);
    sceClibStrncat(buf, "efgh", 2);
    PRINT("append 2 of efgh", sceClibStrcmp(buf, "abcdef") == 0);
    PRINT("set returns the destination", offset(sceClibMemset(buf, 0x1FF, 4), buf));
    PRINT("it stores the low byte", (unsigned char)buf[0] == 0xFF && (unsigned char)buf[3] == 0xFF);
    char move[8] = { '0', '1', '2', '3', '4', '5', '6', '7' };
    PRINT("move forward over itself returns the destination", offset(sceClibMemmove(move + 2, move, 4), move));
    PRINT("it moves as if through a copy", sceClibMemcmp(move, "01012367", 8) == 0);
    char move2[8] = { '0', '1', '2', '3', '4', '5', '6', '7' };
    sceClibMemmove(move2, move2 + 2, 4);
    PRINT("move backward over itself", sceClibMemcmp(move2, "23454567", 8) == 0);
    char copy[4] = { 0 };
    PRINT("copy returns the destination", offset(sceClibMemcpy(copy, "wxyz", 4), copy));
    PRINT("it copies", sceClibMemcmp(copy, "wxyz", 4) == 0);
    PRINT("copy 0 bytes", offset(sceClibMemcpy(copy, "a", 0), copy));

    // Characters
    static const int chars[] = { 'a', 'Z', '5', ' ', '\n', '_', 0x7F, 0xC0, 0xE9 };
    for (unsigned int i = 0; i < sizeof(chars) / sizeof(chars[0]); i++) {
        PRINTF(sceClibTolower((char)chars[i]), "lower case of 0x%02X", chars[i]);
        PRINTF(sceClibToupper((char)chars[i]), "upper case of 0x%02X", chars[i]);
        PRINTF(sceClibLookCtypeTable((char)chars[i]), "character class of 0x%02X", chars[i]);
    }

    test_exit();
    return 0;
}
