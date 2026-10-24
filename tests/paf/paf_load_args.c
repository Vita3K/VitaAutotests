// Loading libpaf with other arguments than homebrew passes: none, a size that doesn't fit, and a heap
// that is too small or too large. A wrong implementation may crash on these, so they have a test of
// their own.
#include "paf.h"

// Loads libpaf with the given arguments, prints the result, and unloads it
static void try_load(const char *what, SceSize args, void *argp) {
    int result = 0;
    const SceSysmoduleOpt opt = { .flags = 0, .result = &result };
    PRINTF(sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, args, argp, &opt), "load with %s", what);
    PRINTF(result, "the result written for %s", what);
    PRINTF(sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF), "paf is loaded after %s", what);
    sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PAF);
    PRINTF(sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF), "paf is loaded after unloading it after %s", what);
}

int main(void) {
    PafInit init = PAF_INIT_DEFAULT;
    try_load("the usual arguments", sizeof(init), &init);

    PafInit small = PAF_INIT_DEFAULT;
    small.global_heap_size = 0x10000;
    try_load("a 64 KiB heap", sizeof(small), &small);

    // A heap of 0 and a size of 4 crash a Vita with C2-12828-1, so they aren't tried
    PafInit usual = PAF_INIT_DEFAULT;
    try_load("a size past the struct", sizeof(usual) + 0x10, &usual);

    test_exit();
    return 0;
}
