// System modules: whether they start loaded, loading and unloading them again, invalid IDs, and a
// module that needs another one
#include <psp2/sysmodule.h>

#include "test.h"

int main(void) {
    static const struct {
        const char *name;
        SceSysmoduleModuleId id;
    } modules[] = {
        { "fiber", SCE_SYSMODULE_FIBER },
        { "ult", SCE_SYSMODULE_ULT },
        { "pgf", SCE_SYSMODULE_PGF },
        { "app util", SCE_SYSMODULE_APPUTIL },
        { "xml", SCE_SYSMODULE_XML },
        { "sqlite", SCE_SYSMODULE_SQLITE },
        { "net", SCE_SYSMODULE_NET },
    };
    for (unsigned int i = 0; i < sizeof(modules) / sizeof(modules[0]); i++) {
        const char *name = modules[i].name;
        const SceSysmoduleModuleId id = modules[i].id;
        PRINTF(sceSysmoduleIsLoaded(id), "%s: loaded at the start", name);
        PRINTF(sceSysmoduleLoadModule(id), "%s: load", name);
        PRINTF(sceSysmoduleIsLoaded(id), "%s: loaded after it", name);
        PRINTF(sceSysmoduleLoadModule(id), "%s: load again", name);
        PRINTF(sceSysmoduleUnloadModule(id), "%s: unload", name);
        PRINTF(sceSysmoduleIsLoaded(id), "%s: loaded after unloading once", name);
        PRINTF(sceSysmoduleUnloadModule(id), "%s: unload again", name);
        PRINTF(sceSysmoduleIsLoaded(id), "%s: loaded after unloading twice", name);
        PRINTF(sceSysmoduleUnloadModule(id), "%s: unload once more", name);
    }

    // A module that needs net, without it and with it
    PRINT("load http without net", sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP));
    PRINT("http is loaded", sceSysmoduleIsLoaded(SCE_SYSMODULE_HTTP));
    sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);
    PRINT("load net", sceSysmoduleLoadModule(SCE_SYSMODULE_NET));
    PRINT("load http with net", sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP));
    PRINT("unload net while http is loaded", sceSysmoduleUnloadModule(SCE_SYSMODULE_NET));
    PRINT("net is loaded", sceSysmoduleIsLoaded(SCE_SYSMODULE_NET));
    PRINT("unload http", sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP));

    // IDs that aren't modules
    static const unsigned int ids[] = { SCE_SYSMODULE_INVALID, 0x36, 0x100, 0x8000, 0xFFFF, 0x10000, 0xFFFFFFFF };
    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        PRINTF(sceSysmoduleLoadModule((SceSysmoduleModuleId)ids[i]), "load module 0x%X", ids[i]);
        PRINTF(sceSysmoduleIsLoaded((SceSysmoduleModuleId)ids[i]), "module 0x%X is loaded", ids[i]);
        PRINTF(sceSysmoduleUnloadModule((SceSysmoduleModuleId)ids[i]), "unload module 0x%X", ids[i]);
    }

    test_exit();
    return 0;
}
