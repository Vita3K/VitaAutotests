// sceAppMgrAppParamGetString: what it returns for each output size around the string's length and
// around the minimum size of the parameter, whether it writes the NUL after the string, and what it
// leaves of the rest of the buffer. The content ID is empty or 36 characters, which differs between
// installs of the vpk, so for it the test only prints that it is one of the two.
#include <psp2/appmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

// The parameter IDs, which older SDK headers don't declare
#define PARAM_CONTENT_ID 6
#define PARAM_NP_COMMUNICATION_ID 7
#define PARAM_CATEGORY 8
#define PARAM_TITLE 9
#define PARAM_SHORT_TITLE 10
#define PARAM_TITLE_ID 12
#define PARAM_CONTENT_ID_OVERRIDE 100
#define PARAM_TITLE_OVERRIDE 101

#define FILL 0xAA
#define GUARD 16
#define MAX_SIZE 0x400

static struct {
    unsigned char before[GUARD];
    char data[MAX_SIZE + 8];
    unsigned char after[GUARD];
} buf;

// Calls with a buffer of size bytes (the guard follows right after them) and prints what happened.
// expected is the string the parameter holds, or NULL when it isn't known.
static void probe(const char *name, int pid, int param, int size, const char *expected) {
    sceClibMemset(&buf, FILL, sizeof(buf));
    // The guard sits right after the size bytes, even for the biggest ones
    unsigned char *end = (unsigned char *)buf.data + (size > MAX_SIZE ? MAX_SIZE : size);
    const int ret = sceAppMgrAppParamGetString(pid, param, buf.data, size);
    PRINTF(ret, "%s, size 0x%X", name, size);
    if (ret < 0)
        return;

    int nul = -1;
    for (int i = 0; i < size && i < MAX_SIZE; i++) {
        if (buf.data[i] == '\0') {
            nul = i;
            break;
        }
    }
    // The content ID is empty or 36 characters, which differs between installs of the vpk
    if (param == PARAM_CONTENT_ID || param == PARAM_CONTENT_ID_OVERRIDE)
        PRINTF(nul == 0 || nul == 0x24, "%s, size 0x%X: the first NUL is at index 0 or 0x24", name, size);
    else
        PRINTF(nul, "%s, size 0x%X: index of the first NUL", name, size);
    if (expected)
        PRINTF(nul >= 0 && sceClibStrncmp(buf.data, expected, nul) == 0 && expected[nul] == '\0', "%s, size 0x%X: holds the string", name, size);

    int rest_untouched = 1;
    for (const unsigned char *p = (const unsigned char *)buf.data + (nul < 0 ? size : nul + 1); p < end; p++)
        rest_untouched &= *p == FILL;
    PRINTF(rest_untouched, "%s, size 0x%X: the rest of the buffer is untouched", name, size);

    int guards_intact = 1;
    for (int i = 0; i < GUARD; i++)
        guards_intact &= buf.before[i] == FILL;
    for (const unsigned char *p = end; p < end + 4; p++)
        guards_intact &= *p == FILL;
    PRINTF(guards_intact, "%s, size 0x%X: guards intact", name, size);
}

int main(void) {
    const int pid = sceKernelGetProcessId();

    // The title ID is "VTST00000", 9 characters
    probe("title id", pid, PARAM_TITLE_ID, 0, "VTST00000");
    probe("title id", pid, PARAM_TITLE_ID, 1, "VTST00000");
    probe("title id", pid, PARAM_TITLE_ID, 9, "VTST00000");
    probe("title id", pid, PARAM_TITLE_ID, 10, "VTST00000");
    probe("title id", pid, PARAM_TITLE_ID, 11, "VTST00000");
    probe("title id", pid, PARAM_TITLE_ID, 0x40, "VTST00000");
    probe("title id", pid, PARAM_TITLE_ID, MAX_SIZE, "VTST00000");
    probe("title id", pid, PARAM_TITLE_ID, MAX_SIZE + 1, "VTST00000");
    probe("title id with pid 0", 0, PARAM_TITLE_ID, 0x40, "VTST00000");

    // The category is "gd" or "gdc", 2 or 3 characters, and needs 4 bytes at least
    probe("category", pid, PARAM_CATEGORY, 2, NULL);
    probe("category", pid, PARAM_CATEGORY, 3, NULL);
    probe("category", pid, PARAM_CATEGORY, 4, NULL);
    probe("category", pid, PARAM_CATEGORY, 5, NULL);

    // The content ID is 36 characters and needs 0x30 bytes at least
    probe("content id", pid, PARAM_CONTENT_ID, 0x24, NULL);
    probe("content id", pid, PARAM_CONTENT_ID, 0x25, NULL);
    probe("content id", pid, PARAM_CONTENT_ID, 0x2F, NULL);
    probe("content id", pid, PARAM_CONTENT_ID, 0x30, NULL);
    probe("content id", pid, PARAM_CONTENT_ID, 0x100, NULL);

    // The title needs 0x80 bytes at least, the short title 0x34
    probe("title", pid, PARAM_TITLE, 0x7F, NULL);
    probe("title", pid, PARAM_TITLE, 0x80, NULL);
    probe("short title", pid, PARAM_SHORT_TITLE, 0x33, NULL);
    probe("short title", pid, PARAM_SHORT_TITLE, 0x34, NULL);
    probe("np communication id", pid, PARAM_NP_COMMUNICATION_ID, 0xC, NULL);
    probe("np communication id", pid, PARAM_NP_COMMUNICATION_ID, 0xD, NULL);

    // The overrides fall back to the content ID and the title
    probe("content id override", pid, PARAM_CONTENT_ID_OVERRIDE, 0x30, NULL);
    probe("title override", pid, PARAM_TITLE_OVERRIDE, 0x80, NULL);

    // Parameters that aren't strings of the SFO
    static const int others[] = { 0, 5, 11, 13, 15, 99, 102, -1 };
    for (int i = 0; i < (int)(sizeof(others) / sizeof(others[0])); i++)
        probe("other parameter", pid, others[i], 0x100, NULL);

    // Last, in case they crash
    PRINT("NULL string", sceAppMgrAppParamGetString(pid, PARAM_TITLE_ID, NULL, 0x40));
    PRINT("NULL string, size 0", sceAppMgrAppParamGetString(pid, PARAM_TITLE_ID, NULL, 0));
    test_exit();
    return 0;
}
