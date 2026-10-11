// A fiber whose entry function returns, which a fiber must not do, in its own test since it may end
// the thread that ran it
#include <psp2/fiber.h>
#include <psp2/sysmodule.h>

#include "test.h"

static SceFiber fiber __attribute__((aligned(8)));
static char context[0x4000] __attribute__((aligned(8)));

static void entry(SceUInt32 arg_on_initialize, SceUInt32 arg_on_run) {
    (void)arg_on_initialize;
    (void)arg_on_run;
}

int main(void) {
    // libfiber is a system module an app loads before using it
    PRINT("load the fiber module", sceSysmoduleLoadModule(SCE_SYSMODULE_FIBER));
    _sceFiberInitializeImpl(&fiber, "returner", entry, 0, context, sizeof(context), NULL);
    SceUInt32 returned = 0xDEAD;
    PRINT("run a fiber whose entry returns", sceFiberRun(&fiber, 0, &returned));
    PRINT("it leaves the return value alone", returned == 0xDEAD);
    PRINT("run it again", sceFiberRun(&fiber, 0, NULL));
    PRINT("finalize it", sceFiberFinalize(&fiber));

    test_exit();
    return 0;
}
