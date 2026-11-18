// sceAppMgrAppParamGetInt: the unknown parameters, the ones that need the shell, and what it writes to out.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>

#include "test.h"

#define FILL 0x5A5A5A5A

int sceAppMgrAppParamGetInt(int pid, int param, int *out);

// Calls with out set to FILL and prints the result and out
static void probe(const char *label, int pid, int param) {
    int out = FILL;
    PRINTF(sceAppMgrAppParamGetInt(pid, param, &out), "%s: result", label);
    PRINTF(out, "%s: out", label);
}

int main(void) {
    const int pid = sceKernelGetProcessId();

    // The parameters the firmware doesn't know, out is written with 0
    probe("param 0", pid, 0);
    probe("param 6", pid, 6);
    probe("param 0xE", pid, 0xE);
    probe("param 0x12", pid, 0x12);
    probe("param -1", pid, -1);
    // The shell only parameters
    probe("param 0xF", pid, 0xF);
    probe("param 0x11", pid, 0x11);
    // Known parameters, whose values depend on the app so only whether they work is printed
    static const int known[] = { 1, 2, 3, 4, 5, 0xB, 0xD, 0x10 };
    for (int i = 0; i < (int)(sizeof(known) / sizeof(known[0])); i++) {
        int out = FILL;
        const int ret = sceAppMgrAppParamGetInt(pid, known[i], &out);
        PRINTF(ret, "param 0x%X: result", known[i]);
        PRINTF(out != FILL, "param 0x%X: out written", known[i]);
    }
    // Another process
    probe("pid 1", 1, 3);
    PRINT("NULL out", sceAppMgrAppParamGetInt(pid, 3, NULL));
    PRINT("NULL out, unknown param", sceAppMgrAppParamGetInt(pid, 0, NULL));
    test_exit();
    return 0;
}
