// _sceAppMgrGetAppState: the size and version checks, the NULL check, and what it writes.
#include <psp2/appmgr.h>
#include <psp2/kernel/clib.h>

#include "test.h"

#define FILL 0xCC
#define STATE_SIZE 0x80
#define MAX_VERSION 0x03730011

static unsigned char buf[0x100];

int main(void) {
    PRINT("size 0x7F", _sceAppMgrGetAppState((SceAppMgrAppState *)buf, 0x7F, MAX_VERSION));
    PRINT("size 0x81", _sceAppMgrGetAppState((SceAppMgrAppState *)buf, 0x81, MAX_VERSION));
    PRINT("size 0", _sceAppMgrGetAppState((SceAppMgrAppState *)buf, 0, MAX_VERSION));
    PRINT("version 0x03730012", _sceAppMgrGetAppState((SceAppMgrAppState *)buf, STATE_SIZE, 0x03730012));
    PRINT("NULL state", _sceAppMgrGetAppState(NULL, STATE_SIZE, MAX_VERSION));
    // The size check comes before the NULL check
    PRINT("NULL state, bad size", _sceAppMgrGetAppState(NULL, 0x7F, MAX_VERSION));

    sceClibMemset(buf, FILL, sizeof(buf));
    PRINT("size 0x80", _sceAppMgrGetAppState((SceAppMgrAppState *)buf, STATE_SIZE, MAX_VERSION));
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

    // An older version is accepted
    PRINT("version 0", _sceAppMgrGetAppState((SceAppMgrAppState *)buf, STATE_SIZE, 0));
    test_exit();
    return 0;
}
