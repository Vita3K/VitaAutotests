// sceClib calls given NULL pointers with lengths of 0, where they need no memory, and NULL strings to
// format with a precision and a width. A test of its own, since an implementation may crash on them.
#include <psp2/kernel/clib.h>

#include "test.h"

#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-overflow"
#pragma GCC diagnostic ignored "-Wnonnull"

static char out[32];

#define FORMAT(...) PRINTF(sceClibSnprintf(out, sizeof(out), __VA_ARGS__), "%s gives [%s]", #__VA_ARGS__, out)

int main(void) {
    PRINT("memcpy of 0 from and to NULL", sceClibMemcpy(NULL, NULL, 0) == NULL);
    PRINT("memcpy_safe of 0 from and to NULL", sceClibMemcpy_safe(NULL, NULL, 0) == NULL);
    PRINT("memmove of 0 from and to NULL", sceClibMemmove(NULL, NULL, 0) == NULL);
    PRINT("memset of 0 at NULL", sceClibMemset(NULL, 0, 0) == NULL);
    PRINT("memcmp of 0 at NULL", sceClibMemcmp(NULL, NULL, 0));
    PRINT("memchr of 0 at NULL", sceClibMemchr(NULL, 'a', 0) == NULL);
    PRINT("strncmp of 0 at NULL", sceClibStrncmp(NULL, NULL, 0));
    PRINT("strncasecmp of 0 at NULL", sceClibStrncasecmp(NULL, NULL, 0));
    PRINT("strnlen of 0 at NULL", sceClibStrnlen(NULL, 0));
    PRINT("strncpy of 0 to NULL", sceClibStrncpy(NULL, "abc", 0) == NULL);
    PRINT("snprintf into NULL of 0 bytes", sceClibSnprintf(NULL, 0, "%d", 12345));

    FORMAT("%.3s|", (const char *)NULL);
    FORMAT("%.0s|", (const char *)NULL);
    FORMAT("%10s|", (const char *)NULL);
    FORMAT("%-10s|", (const char *)NULL);

    test_exit();
    return 0;
}
