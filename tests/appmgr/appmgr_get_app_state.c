// _sceAppMgrGetAppState: the size and version checks, the NULL check, and what it writes. The size
// and NULL checks use version 0, which the call takes.
#include <psp2/appmgr.h>
#include <psp2/kernel/clib.h>

#include "test.h"

#define FILL 0xCC
#define STATE_SIZE 0x80
#define MAX_VERSION 0x03730011
#define OLD_VERSION 0

// Versions to find the highest one the call takes
static const unsigned VERSIONS[] = {
    0x03000000, 0x03500000, 0x03550000, 0x03600000, 0x03650000,
    0x03650011, 0x03680000, 0x03700000, MAX_VERSION,
};

static unsigned char buf[0x100];

int main(void) {
    PRINT("size 0x7F", _sceAppMgrGetAppState((SceAppMgrAppState *)buf, 0x7F, OLD_VERSION));
    PRINT("size 0x81", _sceAppMgrGetAppState((SceAppMgrAppState *)buf, 0x81, OLD_VERSION));
    PRINT("size 0", _sceAppMgrGetAppState((SceAppMgrAppState *)buf, 0, OLD_VERSION));
    PRINT("NULL state", _sceAppMgrGetAppState(NULL, STATE_SIZE, OLD_VERSION));
    PRINT("NULL state, bad size", _sceAppMgrGetAppState(NULL, 0x7F, OLD_VERSION));
    for (int i = 0; i < (int)(sizeof(VERSIONS) / sizeof(VERSIONS[0])); i++)
        PRINTF(_sceAppMgrGetAppState((SceAppMgrAppState *)buf, STATE_SIZE, VERSIONS[i]), "version 0x%08X", VERSIONS[i]);
    PRINT("version 0x03730012", _sceAppMgrGetAppState((SceAppMgrAppState *)buf, STATE_SIZE, 0x03730012));

    sceClibMemset(buf, FILL, sizeof(buf));
    PRINT("size 0x80", _sceAppMgrGetAppState((SceAppMgrAppState *)buf, STATE_SIZE, OLD_VERSION));
    int rest_untouched = 1;
    for (int i = STATE_SIZE; i < (int)sizeof(buf); i++)
        rest_untouched &= buf[i] == FILL;
    PRINT("bytes after 0x80 untouched", rest_untouched);
    const unsigned *words = (const unsigned *)buf;
    PRINT("pending event count", words[0]);
    PRINT("word 1", words[1]);
    PRINT("word 2", words[2]);
    int zero_tail = 1;
    for (int i = 12; i < STATE_SIZE; i++)
        zero_tail &= buf[i] == 0;
    PRINT("bytes 12 to 0x7F are zero", zero_tail);

    // A failed call still writes the 0x80 bytes
    sceClibMemset(buf, FILL, sizeof(buf));
    PRINT("size 0x7F into a filled buffer", _sceAppMgrGetAppState((SceAppMgrAppState *)buf, 0x7F, OLD_VERSION));
    PRINT("the first word after it", words[0]);
    test_exit();
    return 0;
}
