// The ScePafStdc number calls: parsing integers and floats with where they stop, abs, the random
// number sequence of a seed, sorting, binary search and the character table
#include "paf.h"

static int compare_ints(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

// Prints a float as thousandths, so the output doesn't depend on how a double prints
static int thousandths(double value) {
    return (int)(value * 1000.0);
}

int main(void) {
    paf_load_or_skip();

    // Integers
    PRINT("atoi 123", sce_paf_atoi("123"));
    PRINT("atoi -45", sce_paf_atoi("-45"));
    PRINT("atoi with spaces and a sign", sce_paf_atoi("  +7x"));
    PRINT("atoi of text", sce_paf_atoi("abc"));
    PRINT("atoi of 2147483648", sce_paf_atoi("2147483648"));
    PRINT("atol 123456", sce_paf_atol("123456"));
    PRINT("atoll 4294967296 low", (int)sce_paf_atoll("4294967296"));
    PRINT("atoll 4294967296 high", (int)(sce_paf_atoll("4294967296") >> 32));
    PRINT("abs of -5", sce_paf_abs(-5));
    PRINT("abs of 5", sce_paf_abs(5));
    PRINT("abs of the lowest int", sce_paf_abs((int)0x80000000));

    static const struct {
        const char *text;
        int base;
    } parses[] = {
        { "123abc", 10 }, { "  -42", 10 }, { "0x1F", 16 }, { "0x1F", 0 }, { "017", 0 }, { "017", 10 }, { "zz", 36 },
        { "12", 2 }, { "", 10 }, { "-", 10 }, { "99999999999", 10 }, { "-99999999999", 10 }, { "10", 1 }, { "10", 37 },
    };
    for (unsigned int i = 0; i < sizeof(parses) / sizeof(parses[0]); i++) {
        char *end = (char *)-1;
        const char *text = parses[i].text;
        PRINTF(sce_paf_strtol(text, &end, parses[i].base), "strtol \"%s\" base %d", text, parses[i].base);
        PRINTF(end == (char *)-1 ? -2 : (int)(end - text), "it stops at");
        end = (char *)-1;
        PRINTF(sce_paf_strtoul(text, &end, parses[i].base), "strtoul \"%s\" base %d", text, parses[i].base);
        PRINTF((int)sce_paf_strtoll(text, NULL, parses[i].base), "strtoll \"%s\" base %d low", text, parses[i].base);
        PRINTF((int)(sce_paf_strtoll(text, NULL, parses[i].base) >> 32), "strtoll \"%s\" base %d high", text, parses[i].base);
        PRINTF((int)sce_paf_strtoull(text, NULL, parses[i].base), "strtoull \"%s\" base %d low", text, parses[i].base);
    }

    // Floats
    PRINT("atof 3.25", thousandths(sce_paf_atof("3.25")));
    PRINT("atof -0.5", thousandths(sce_paf_atof("-0.5")));
    PRINT("atof 1e3", thousandths(sce_paf_atof("1e3")));
    PRINT("atof of text", thousandths(sce_paf_atof("abc")));
    static const char *floats[] = { "2.5xyz", "  -1.75", ".5", "1e-2", "0x10", "inf", "", "1e400" };
    for (unsigned int i = 0; i < sizeof(floats) / sizeof(floats[0]); i++) {
        char *end = (char *)-1;
        const double value = sce_paf_strtod(floats[i], &end);
        PRINTF(thousandths(value > 1e9 ? 1e9 : value), "strtod \"%s\"", floats[i]);
        PRINTF(end == (char *)-1 ? -2 : (int)(end - floats[i]), "it stops at");
    }

    // The random numbers of a seed
    sce_paf_srand(1);
    int first[4];
    for (int i = 0; i < 4; i++)
        first[i] = sce_paf_rand();
    sce_paf_srand(1);
    PRINT("the same seed gives the same first number", sce_paf_rand() == first[0]);
    PRINT("and the same second number", sce_paf_rand() == first[1]);
    sce_paf_srand(2);
    PRINT("another seed gives another first number", sce_paf_rand() != first[0]);
    sce_paf_srand(1);
    for (int i = 0; i < 4; i++)
        PRINTF(sce_paf_rand(), "seed 1 number %d", i);
    sce_paf_srand(0);
    for (int i = 0; i < 2; i++)
        PRINTF(sce_paf_rand(), "seed 0 number %d", i);

    // Sorting and searching
    int values[] = { 5, 3, 9, 1, 7, -2, 3, 0 };
    sce_paf_qsort(values, 8, sizeof(int), compare_ints);
    for (int i = 0; i < 8; i++)
        PRINTF(values[i], "sorted value %d", i);
    int key = 7;
    PRINT("find 7", (int *)sce_paf_bsearch(&key, values, 8, sizeof(int), compare_ints) - values);
    key = 4;
    PRINT("find 4", sce_paf_bsearch(&key, values, 8, sizeof(int), compare_ints) != NULL);
    key = -2;
    PRINT("find -2", (int *)sce_paf_bsearch(&key, values, 8, sizeof(int), compare_ints) - values);
    PRINT("find in 0 values", sce_paf_bsearch(&key, values, 0, sizeof(int), compare_ints) != NULL);
    int one[] = { 2, 1 };
    sce_paf_qsort(one, 1, sizeof(int), compare_ints);
    PRINT("sort 1 value leaves them", one[0] == 2 && one[1] == 1);
    sce_paf_qsort(one, 0, sizeof(int), compare_ints);
    PRINT("sort 0 values leaves them", one[0] == 2 && one[1] == 1);

    // The character table, which takes a character, and returns what the table holds for it
    static const int chars[] = { -1, 0, '\t', ' ', '0', '9', 'A', 'Z', 'a', 'z', '_', '~', 0x7F, 0x80, 0xC0, 0xFF, 0x100 };
    for (unsigned int i = 0; i < sizeof(chars) / sizeof(chars[0]); i++)
        PRINTF(sce_paf_look_ctype_table(chars[i]), "ctype table of 0x%X", (unsigned)chars[i]);

    test_exit();
    return 0;
}
