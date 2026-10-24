// Loading libpaf as an internal system module: whether it starts loaded, loading it with the arguments
// homebrew passes, loading it again, and unloading it
#include "paf.h"

int main(void) {
    PRINT("paf is loaded at the start", sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF));
    PRINT("load paf", paf_load());
    PRINT("paf is loaded after it", sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF));
    PRINT("load paf again", paf_load());
    PRINT("a PAF call works", sce_paf_strlen("abc"));
    PRINT("unload paf", sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PAF));
    PRINT("paf is loaded after unloading once", sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF));
    PRINT("unload paf again", sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PAF));
    PRINT("paf is loaded after unloading twice", sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF));
    PRINT("unload paf once more", sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PAF));

    test_exit();
    return 0;
}
