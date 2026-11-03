// SceLibc qsort, qsort_s, bsearch and bsearch_s: the order they leave,
// which of equal keys go first, how often they compare short arrays, which of equal keys a search
// finds, and the runtime constraints of the _s calls with the handler they call
#include "scelibc.h"
#include "test.h"

static int compares;

static int compare_int(const void *a, const void *b) {
    compares++;
    const int x = *(const int *)a, y = *(const int *)b;
    return x < y ? -1 : x > y;
}

// Compares by subtracting, so it returns values far from -1 and 1
static int compare_int_difference(const void *a, const void *b) {
    compares++;
    return (*(const int *)a >> 1) - (*(const int *)b >> 1);
}

static void *seen_context;
static int context_marker;

static int compare_int_context(const void *a, const void *b, void *context) {
    seen_context = context;
    return compare_int(a, b);
}

struct record {
    int key;
    int id;
};

static int compare_record(const void *a, const void *b) {
    return ((const struct record *)a)->key - ((const struct record *)b)->key;
}

// Elements of an odd size, which can't be moved as words
struct odd {
    char bytes[37];
};

static int compare_odd(const void *a, const void *b) {
    return sceClibMemcmp(a, b, sizeof(struct odd));
}

static int is_sorted(const int *values, int count) {
    for (int i = 1; i < count; i++) {
        if (values[i - 1] > values[i])
            return 0;
    }
    return 1;
}

static int handler_calls;
static int handler_error;

static void handler(const char *msg, void *ptr, int error) {
    (void)msg;
    (void)ptr;
    handler_calls++;
    handler_error = error;
}

// Prints what an _s call returned, and the handler calls and errno it left
#define PRINT_CONSTRAINT(name, call)                                         \
    do {                                                                     \
        handler_calls = 0;                                                   \
        handler_error = 0;                                                   \
        *scelibc__sceLibcErrnoLoc() = 0;                                     \
        PRINT(name, call);                                                   \
        PRINT(name ": handler calls", handler_calls);                        \
        PRINT(name ": error passed to the handler", handler_error);          \
        PRINT(name ": errno", *scelibc__sceLibcErrnoLoc());                  \
    } while (0)

int main(void) {

    // Order
    int values[16] = { 5, -3, 12, 0, 5, 99, -100, 7, 7, 1, 0x7FFFFFFF, (int)0x80000000, 3, 3, 3, 2 };
    scelibc_qsort(values, 16, sizeof(int), compare_int);
    PRINT("sort 16 ints", is_sorted(values, 16));
    PRINT("the smallest", values[0]);
    PRINT("the largest", values[15]);
    int ascending[100], descending[100], same[100];
    for (int i = 0; i < 100; i++) {
        ascending[i] = i;
        descending[i] = 100 - i;
        same[i] = 4;
    }
    scelibc_qsort(ascending, 100, sizeof(int), compare_int);
    PRINT("sort 100 sorted ints", is_sorted(ascending, 100));
    scelibc_qsort(descending, 100, sizeof(int), compare_int);
    PRINT("sort 100 reversed ints", is_sorted(descending, 100) && descending[0] == 1);
    scelibc_qsort(same, 100, sizeof(int), compare_int);
    PRINT("sort 100 equal ints", same[0] == 4 && same[99] == 4);
    int differences[8] = { 1000000, -1000000, 6, 2, 400, -2, 0, 8 };
    scelibc_qsort(differences, 8, sizeof(int), compare_int_difference);
    PRINT("sort with a comparison that subtracts", differences[0] == -1000000 && differences[7] == 1000000);
    struct odd odds[9];
    for (int i = 0; i < 9; i++)
        sceClibMemset(&odds[i], 'a' + (i * 5) % 9, sizeof(odds[i]));
    scelibc_qsort(odds, 9, sizeof(struct odd), compare_odd);
    int odd_sorted = 1;
    for (int i = 0; i < 9; i++)
        odd_sorted &= odds[i].bytes[0] == 'a' + i && odds[i].bytes[36] == 'a' + i;
    PRINT("sort elements of 37 bytes", odd_sorted);

    // Which of equal keys go first. qsort doesn't promise an order, so this is what the Vita's does.
    struct record records[12];
    static const int keys[12] = { 3, 1, 2, 3, 1, 2, 3, 1, 2, 0, 3, 1 };
    for (int i = 0; i < 12; i++) {
        records[i].key = keys[i];
        records[i].id = i;
    }
    scelibc_qsort(records, 12, sizeof(struct record), compare_record);
    for (int i = 0; i < 12; i++)
        PRINTF(records[i].id, "sorted record %d: its original index", i);

    // How often it compares
    static const int counts[] = { 0, 1, 2, 3, 4, 7, 8, 16 };
    for (unsigned int i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
        int small[16];
        for (int j = 0; j < counts[i]; j++)
            small[j] = (j * 7) % 5;
        compares = 0;
        scelibc_qsort(small, counts[i], sizeof(int), compare_int);
        PRINTF(compares, "sort %d ints: compares", counts[i]);
        PRINTF(is_sorted(small, counts[i]), "sort %d ints: sorted", counts[i]);
    }
    compares = 0;
    scelibc_qsort(NULL, 0, sizeof(int), compare_int);
    PRINT("sort 0 ints at NULL: compares", compares);

    // Searching
    int sorted[9] = { 1, 3, 3, 3, 5, 7, 9, 9, 11 };
    for (int key = 0; key <= 12; key++) {
        const int *found = scelibc_bsearch(&key, sorted, 9, sizeof(int), compare_int);
        PRINTF(found ? (int)(found - sorted) : -1, "search 9 ints for %d", key);
    }
    const int key = 3;
    PRINT("search 0 ints", scelibc_bsearch(&key, sorted, 0, sizeof(int), compare_int) == NULL);
    PRINT("search 1 int", scelibc_bsearch(&key, sorted + 1, 1, sizeof(int), compare_int) == sorted + 1);

    // The _s calls with valid arguments
    const scelibc_constraint_handler old = scelibc_set_constraint_handler_s(handler);
    PRINT("the default constraint handler is set", old != NULL);
    int with_context[6] = { 4, 2, 6, 1, 5, 3 };
    seen_context = NULL;
    PRINT_CONSTRAINT("sort 6 ints with qsort_s", scelibc_qsort_s(with_context, 6, sizeof(int), compare_int_context, &context_marker));
    PRINT("they are sorted", is_sorted(with_context, 6));
    PRINT("the comparison got the context", seen_context == &context_marker);
    seen_context = NULL;
    const int *found = scelibc_bsearch_s(&key, with_context, 6, sizeof(int), compare_int_context, &values);
    PRINT("search them with bsearch_s", found ? (int)(found - with_context) : -1);
    PRINT("the comparison got the context", seen_context == &values);
    PRINT_CONSTRAINT("qsort_s of 0 ints at NULL", scelibc_qsort_s(NULL, 0, sizeof(int), compare_int_context, NULL));

    // The _s calls breaking their constraints, with the handler set above
    PRINT_CONSTRAINT("qsort_s of 2 ints at NULL", scelibc_qsort_s(NULL, 2, sizeof(int), compare_int_context, NULL));
    PRINT_CONSTRAINT("qsort_s without a comparison", scelibc_qsort_s(with_context, 6, sizeof(int), NULL, NULL));
    PRINT_CONSTRAINT("qsort_s of 0x80000000 ints", scelibc_qsort_s(with_context, 0x80000000u, sizeof(int), compare_int_context, NULL));
    PRINT_CONSTRAINT("qsort_s of ints of 0x80000000 bytes", scelibc_qsort_s(with_context, 2, 0x80000000u, compare_int_context, NULL));
    PRINT_CONSTRAINT("qsort_s of ints of 0 bytes", scelibc_qsort_s(with_context, 2, 0, compare_int_context, NULL));
    PRINT_CONSTRAINT("bsearch_s without a key", scelibc_bsearch_s(NULL, with_context, 6, sizeof(int), compare_int_context, NULL) == NULL);
    PRINT_CONSTRAINT("bsearch_s of 6 ints at NULL", scelibc_bsearch_s(&key, NULL, 6, sizeof(int), compare_int_context, NULL) == NULL);
    PRINT_CONSTRAINT("bsearch_s without a comparison", scelibc_bsearch_s(&key, with_context, 6, sizeof(int), NULL, NULL) == NULL);
    PRINT_CONSTRAINT("bsearch_s of 0 ints at NULL", scelibc_bsearch_s(&key, NULL, 0, sizeof(int), compare_int_context, NULL) == NULL);

    // The library's handler that ignores them
    scelibc_set_constraint_handler_s(scelibc_ignore_handler_s);
    *scelibc__sceLibcErrnoLoc() = 0;
    PRINT("qsort_s of 2 ints at NULL with the ignoring handler", scelibc_qsort_s(NULL, 2, sizeof(int), compare_int_context, NULL));
    PRINT("its errno", *scelibc__sceLibcErrnoLoc());
    PRINT("put the first handler back", scelibc_set_constraint_handler_s(old) == scelibc_ignore_handler_s);

    test_exit();
    return 0;
}
