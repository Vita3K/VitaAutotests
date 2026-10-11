// Fibers: initialization rules, running and switching between fibers, what each side gets back, and
// the errors for calls made in the wrong place. Fibers record what they see and the thread prints it.
#include <psp2/fiber.h>
#include <psp2/sysmodule.h>

#include "test.h"

#define CONTEXT_SIZE 0x4000

// The vitasdk declaration takes a SceFiber *, the call writes a SceFiber *
SceInt32 fiber_get_self(SceFiber **fiber) __asm__("sceFiberGetSelf");

static SceFiber fiber_a __attribute__((aligned(8))), fiber_b __attribute__((aligned(8)));
static char context_a[CONTEXT_SIZE] __attribute__((aligned(8))), context_b[CONTEXT_SIZE] __attribute__((aligned(8)));

static SceUInt32 seen_init_arg, seen_run_arg, seen_resume_arg, b_run_arg;
static int self_result, self_is_a, run_self, a_after_switch;

// Returns to the thread twice, then switches to fiber_b, which returns to the thread for both
static void entry_a(SceUInt32 arg_on_initialize, SceUInt32 arg_on_run) {
    seen_init_arg = arg_on_initialize;
    seen_run_arg = arg_on_run;
    SceFiber *self = NULL;
    self_result = fiber_get_self(&self);
    self_is_a = self == &fiber_a;
    run_self = sceFiberRun(&fiber_a, 0, NULL);
    sceFiberReturnToThread(0x55, &seen_resume_arg);
    SceUInt32 back = 0;
    sceFiberSwitch(&fiber_b, 0x77, &back);
    a_after_switch = back;
    for (;;)
        sceFiberReturnToThread(0x99, NULL);
}

static void entry_b(SceUInt32 arg_on_initialize, SceUInt32 arg_on_run) {
    (void)arg_on_initialize;
    b_run_arg = arg_on_run;
    sceFiberSwitch(&fiber_a, 0x88, NULL);
    for (;;)
        sceFiberReturnToThread(0xBB, NULL);
}

// An entry function that returns, which a fiber must not do
static void entry_returns(SceUInt32 arg_on_initialize, SceUInt32 arg_on_run) {
    (void)arg_on_initialize;
    (void)arg_on_run;
}

// Returns 0 if a fiber can be initialized with this context, or the error
static int init_result(void *context, SceSize size) {
    static SceFiber fiber __attribute__((aligned(8)));
    const int ret = _sceFiberInitializeImpl(&fiber, "probe", entry_returns, 0, context, size, NULL);
    if (ret == 0)
        sceFiberFinalize(&fiber);
    return ret;
}

int main(void) {
    // libfiber is a system module an app loads before using it
    PRINT("load the fiber module", sceSysmoduleLoadModule(SCE_SYSMODULE_FIBER));
    // Context rules
    PRINT("initialize with a 512 byte context", init_result(context_b, 512));
    PRINT("initialize with a 504 byte context", init_result(context_b, 504));
    PRINT("initialize with a 516 byte context", init_result(context_b, 516));
    PRINT("initialize with a misaligned context", init_result(context_b + 4, 512));
    PRINT("initialize with a context and size 0", init_result(context_b, 0));
    PRINT("initialize without a context or size", init_result(NULL, 0));
    PRINT("initialize without a context but a size", init_result(NULL, 512));
    static char misaligned[sizeof(SceFiber) + 8] __attribute__((aligned(8)));
    PRINT("initialize a misaligned fiber", _sceFiberInitializeImpl((SceFiber *)(misaligned + 4), "m", entry_returns, 0, context_b, 512, NULL));

    // Calls that only work inside a fiber
    SceFiber *self = (SceFiber *)0xDEAD;
    PRINT("get the running fiber from a thread", fiber_get_self(&self));
    PRINT("it writes NULL", self == NULL);
    PRINT("return to the thread from a thread", sceFiberReturnToThread(0, NULL));

    // Running fiber A
    PRINT("initialize fiber A", _sceFiberInitializeImpl(&fiber_a, "fiber a", entry_a, 0x11, context_a, CONTEXT_SIZE, NULL));
    PRINT("initialize it again", _sceFiberInitializeImpl(&fiber_a, "fiber a", entry_a, 0x11, context_a, CONTEXT_SIZE, NULL));
    PRINT("initialize fiber B", _sceFiberInitializeImpl(&fiber_b, "fiber b", entry_b, 0x22, context_b, CONTEXT_SIZE, NULL));
    PRINT("switch to a fiber from a thread", sceFiberSwitch(&fiber_b, 0, NULL));
    SceUInt32 returned = 0xDEAD;
    PRINT("run fiber A", sceFiberRun(&fiber_a, 0x33, &returned));
    PRINT("what it got at initialization", seen_init_arg);
    PRINT("what it got when run", seen_run_arg);
    PRINT("get the running fiber inside it", self_result);
    PRINT("that is fiber A", self_is_a);
    PRINT("run itself from inside", run_self);
    PRINT("what it returned to the thread", returned);

    SceFiberInfo info;
    sceClibMemset(&info, 0, sizeof(info));
    PRINT("info of a suspended fiber", sceFiberGetInfo(&fiber_a, &info));
    PRINT("its entry", info.entry == entry_a);
    PRINT("its argument", info.argOnInitialize);
    PRINT("its context", info.addrContext == context_a);
    PRINT("its context size", info.sizeContext);
    PRINT("its name", sceClibStrcmp(info.name, "fiber a") == 0);

    // Resuming it, then switching between the two
    returned = 0xDEAD;
    PRINT("run it again", sceFiberRun(&fiber_a, 0x44, &returned));
    PRINT("what it got when resumed", seen_resume_arg);
    PRINT("what fiber B got when switched to", b_run_arg);
    PRINT("what fiber A got back from B", a_after_switch);
    PRINT("what A returned to the thread", returned);
    returned = 0xDEAD;
    PRINT("run fiber B, which A left suspended", sceFiberRun(&fiber_b, 0, &returned));
    PRINT("what B returned to the thread", returned);

    // Finalizing
    PRINT("finalize fiber A", sceFiberFinalize(&fiber_a));
    PRINT("finalize it again", sceFiberFinalize(&fiber_a));
    PRINT("run a finalized fiber", sceFiberRun(&fiber_a, 0, NULL));
    PRINT("info of a finalized fiber", sceFiberGetInfo(&fiber_a, &info));
    PRINT("finalize fiber B", sceFiberFinalize(&fiber_b));

    // Finalizing a fiber that is suspended in the middle of its entry function
    static SceFiber fiber_c __attribute__((aligned(8)));
    _sceFiberInitializeImpl(&fiber_c, "fiber c", entry_b, 0, context_b, CONTEXT_SIZE, NULL);
    sceFiberRun(&fiber_c, 0, NULL);
    PRINT("finalize a suspended fiber", sceFiberFinalize(&fiber_c));
    PRINT("run it after that", sceFiberRun(&fiber_c, 0, NULL));

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("initialize a NULL fiber", _sceFiberInitializeImpl(NULL, "n", entry_returns, 0, context_b, 512, NULL));
    PRINT("initialize with a NULL name", _sceFiberInitializeImpl(&fiber_b, NULL, entry_returns, 0, context_b, 512, NULL));
    PRINT("initialize with a NULL entry", _sceFiberInitializeImpl(&fiber_a, "n", NULL, 0, context_a, 512, NULL));
    PRINT("run a NULL fiber", sceFiberRun(NULL, 0, NULL));
    PRINT("info into a NULL pointer", sceFiberGetInfo(&fiber_b, NULL));
    PRINT("get the running fiber into a NULL pointer", fiber_get_self(NULL));

    test_exit();
    return 0;
}
