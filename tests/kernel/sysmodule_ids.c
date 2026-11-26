// sceSysmoduleIsLoaded and sceSysmoduleIsLoadedInternal for every id, before any load. An id the
// app can't use gives 0x805A1000, one it can use but hasn't loaded gives 0x805A1001.
#include <psp2/sysmodule.h>

#include "test.h"

int main(void) {
    for (int id = 0; id <= 0x57; id++)
        PRINTF(sceSysmoduleIsLoaded(id), "is loaded 0x%02X", id);
    for (int id = 0; id <= 0x2B; id++)
        PRINTF(sceSysmoduleIsLoadedInternal(0x80000000 | id), "internal is loaded 0x%08X", 0x80000000 | id);
    test_exit();
    return 0;
}
