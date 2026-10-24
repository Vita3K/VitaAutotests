// The ScePafStdc string calls: results of comparisons, searches as offsets, copying, appending,
// the sizes of the size bounded ones, spans and tokenizing. sce_paf_private_strtok is left out, it
// doesn't take the arguments of strtok.
#include "paf.h"

// Returns where p points into base, or -1 for NULL
static int offset(const void *p, const void *base) {
    return p ? (int)((const char *)p - (const char *)base) : -1;
}

int main(void) {
    paf_load_or_skip();

    // Length
    PRINT("the length of hello", sce_paf_strlen("hello"));
    PRINT("the length of an empty string", sce_paf_strlen(""));

    // Comparing
    PRINT("compare abc with abd", sce_paf_strcmp("abc", "abd"));
    PRINT("compare abd with abc", sce_paf_strcmp("abd", "abc"));
    PRINT("compare abc with abc", sce_paf_strcmp("abc", "abc"));
    PRINT("compare ab with abc", sce_paf_strcmp("ab", "abc"));
    PRINT("compare abc with ab", sce_paf_strcmp("abc", "ab"));
    PRINT("compare a high byte with a low one", sce_paf_strcmp("\xC0", "A"));
    PRINT("compare the first 2 of abc and abd", sce_paf_strncmp("abc", "abd", 2));
    PRINT("compare the first 3 of abc and abd", sce_paf_strncmp("abc", "abd", 3));
    PRINT("compare 0 characters", sce_paf_strncmp("abc", "xyz", 0));
    PRINT("compare ABC with abc ignoring case", sce_paf_strcasecmp("ABC", "abc"));
    PRINT("compare ABC with abd ignoring case", sce_paf_strcasecmp("ABC", "abd"));
    PRINT("compare abd with ABC ignoring case", sce_paf_strcasecmp("abd", "ABC"));
    PRINT("compare [ with a ignoring case", sce_paf_strcasecmp("[", "a"));
    PRINT("compare the first 2 of ABC and abd ignoring case", sce_paf_strncasecmp("ABC", "abd", 2));
    PRINT("compare the first 3 of ABC and abd ignoring case", sce_paf_strncasecmp("ABC", "abd", 3));
    PRINT("compare 0 characters ignoring case", sce_paf_strncasecmp("abc", "xyz", 0));

    // Searching
    const char *text = "hello world, hello";
    PRINT("find the first l", offset(sce_paf_strchr(text, 'l'), text));
    PRINT("find the last l", offset(sce_paf_strrchr(text, 'l'), text));
    PRINT("find a missing character", offset(sce_paf_strchr(text, 'z'), text));
    PRINT("find the terminator", offset(sce_paf_strchr(text, '\0'), text));
    PRINT("find the last terminator", offset(sce_paf_strrchr(text, '\0'), text));
    PRINT("find a character past 0xFF", offset(sce_paf_strchr(text, 0x100 + 'w'), text));
    PRINT("find a byte", offset(sce_paf_memchr(text, 'w', 18), text));
    PRINT("find a byte past the length", offset(sce_paf_memchr(text, 'w', 5), text));
    PRINT("find the first of o and w", offset(sce_paf_strpbrk(text, "ow"), text));
    PRINT("find the first of z and q", offset(sce_paf_strpbrk(text, "zq"), text));
    PRINT("find the first of an empty set", offset(sce_paf_strpbrk(text, ""), text));

    // Spans
    PRINT("the span of hel in hello world", sce_paf_strspn(text, "hel"));
    PRINT("the span of nothing", sce_paf_strspn(text, "xyz"));
    PRINT("the span of an empty set", sce_paf_strspn(text, ""));
    PRINT("the span of the whole string", sce_paf_strspn("aab", "ab"));
    PRINT("the span without o", sce_paf_strcspn(text, "o"));
    PRINT("the span without z", sce_paf_strcspn(text, "z"));
    PRINT("the span without an empty set", sce_paf_strcspn(text, ""));
    PRINT("the span without h", sce_paf_strcspn(text, "h"));

    // Copying
    char buf[16];
    sce_paf_memset(buf, 'x', sizeof(buf));
    PRINT("copy abc returns the destination", offset(sce_paf_strcpy(buf, "abc"), buf));
    PRINT("it copies the terminator", buf[3] == 0 && buf[4] == 'x');
    sce_paf_memset(buf, 'x', sizeof(buf));
    PRINT("copy abc into 8 bytes returns the destination", offset(sce_paf_strncpy(buf, "abc", 8), buf));
    PRINT("it pads the rest with zeros", buf[3] == 0 && buf[7] == 0 && buf[8] == 'x');
    sce_paf_memset(buf, 'x', sizeof(buf));
    sce_paf_strncpy(buf, "abcdef", 3);
    PRINT("copy 3 of abcdef leaves no terminator", buf[2] == 'c' && buf[3] == 'x');
    sce_paf_memset(buf, 'x', sizeof(buf));
    PRINT("copy abc with a size of 8 returns its length", sce_paf_strlcpy(buf, "abc", 8));
    PRINT("it pads nothing", buf[3] == 0 && buf[4] == 'x');
    sce_paf_memset(buf, 'x', sizeof(buf));
    PRINT("copy abcdef with a size of 4 returns its length", sce_paf_strlcpy(buf, "abcdef", 4));
    PRINT("it cuts it and ends it", buf[2] == 'c' && buf[3] == 0 && buf[4] == 'x');
    sce_paf_memset(buf, 'x', sizeof(buf));
    PRINT("copy abc with a size of 0 returns its length", sce_paf_strlcpy(buf, "abc", 0));
    PRINT("it writes nothing", buf[0] == 'x');
    sce_paf_memset(buf, 'x', sizeof(buf));
    PRINT("copy abc with a size of 1 returns its length", sce_paf_strlcpy(buf, "abc", 1));
    PRINT("it only writes the terminator", buf[0] == 0 && buf[1] == 'x');

    // Appending
    sce_paf_memset(buf, 0, sizeof(buf));
    sce_paf_strcpy(buf, "ab");
    PRINT("append cd returns the destination", offset(sce_paf_strcat(buf, "cd"), buf));
    PRINT("it appends", sce_paf_strcmp(buf, "abcd") == 0);
    sce_paf_strncat(buf, "efgh", 2);
    PRINT("append 2 of efgh", sce_paf_strcmp(buf, "abcdef") == 0);
    sce_paf_strncat(buf, "gh", 8);
    PRINT("append gh with a length of 8", sce_paf_strcmp(buf, "abcdefgh") == 0);
    PRINT("it ends the string", buf[8] == 0);
    sce_paf_memset(buf, 0, sizeof(buf));
    sce_paf_strcpy(buf, "ab");
    PRINT("append cd with a size of 8 returns the length it would have", sce_paf_strlcat(buf, "cd", 8));
    PRINT("it appends it", sce_paf_strcmp(buf, "abcd") == 0);
    PRINT("append efghij with a size of 8 returns the length it would have", sce_paf_strlcat(buf, "efghij", 8));
    PRINT("it cuts it", sce_paf_strcmp(buf, "abcdefg") == 0);
    PRINT("append x with a size of 3, short of the destination's length", sce_paf_strlcat(buf, "x", 3));
    PRINT("it leaves the destination", sce_paf_strcmp(buf, "abcdefg") == 0);

    // Tokenizing
    char tokens[] = "a,b;;c,";
    char *save = NULL;
    char *token = sce_paf_strtok_r(tokens, ",;", &save);
    for (int i = 0; i < 5; i++) {
        PRINTF(token ? token[0] : -1, "token %d", i);
        token = sce_paf_strtok_r(NULL, ",;", &save);
    }

    // Case
    PRINT("lower A", sce_paf_tolower('A'));
    PRINT("lower a", sce_paf_tolower('a'));
    PRINT("lower 0", sce_paf_tolower('0'));
    PRINT("lower [", sce_paf_tolower('['));
    PRINT("upper z", sce_paf_toupper('z'));
    PRINT("upper Z", sce_paf_toupper('Z'));
    PRINT("upper {", sce_paf_toupper('{'));
    PRINT("lower a high byte", sce_paf_tolower(0xC0));
    PRINT("upper a high byte", sce_paf_toupper(0xE0));
    PRINT("lower -1", sce_paf_tolower(-1));
    PRINT("upper 0x161", sce_paf_toupper(0x161));

    test_exit();
    return 0;
}
