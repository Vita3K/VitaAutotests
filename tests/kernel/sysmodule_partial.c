// Sysmodule loads and unloads that fail partway, because the process has no room for the thread that
// runs a module's start or stop function, and what a retry does then. A thread event handler records
// the name of every thread that starts, so the threads of module start and stop show which modules
// a call starts or stops. The modules of the test process are listed by name after each step.
//
// LOCATION_EXTENSION loads liblocation, which has only a stop function, then liblocation_extension,
// which has only a start function. Both are modules of the process alone. The network modules are
// shared, and a failed start of one leaves a module behind that later runs see, so they aren't used.
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>

#include "test.h"

#define CAP 256
#define MAX_STARTS 8
#define MAX_MODULES 64

static SceUID threads[CAP];
static int thread_count;

static char starts[MAX_STARTS][32];
static volatile int start_count;

static SceUID first_modules[MAX_MODULES];
static SceSize first_count;

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

static int handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)arg;
    (void)common;
    const int i = start_count;
    if (i >= MAX_STARTS)
        return 0;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread_id, &info);
    sceClibStrncpy(starts[i], info.name, sizeof(starts[i]) - 1);
    start_count = i + 1;
    return 0;
}

// Creates threads until a create fails and returns that error
static int fill(void) {
    while (thread_count < CAP) {
        const SceUID thread = sceKernelCreateThread("fill", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x1000, 0, 0, NULL);
        if (thread < 0)
            return thread;
        threads[thread_count++] = thread;
    }
    return 0;
}

static void free_one(void) {
    if (thread_count > 0)
        sceKernelDeleteThread(threads[--thread_count]);
}

static void free_all(void) {
    while (thread_count > 0)
        free_one();
}

static int was_there(SceUID module) {
    for (SceSize i = 0; i < first_count; i++) {
        if (first_modules[i] == module)
            return 1;
    }
    return 0;
}

// Prints the result, the threads that started during the call and the modules loaded since main began
static void print_call(const char *what, int result) {
    PRINTF(result, "%s", what);
    PRINTF(start_count, "%s: threads that started", what);
    for (int i = 0; i < start_count && i < MAX_STARTS; i++)
        PRINTF(1, "%s: thread %d is %s", what, i + 1, starts[i]);
    sceClibMemset(starts, 0, sizeof(starts));
    start_count = 0;

    SceUID modules[MAX_MODULES];
    SceSize count = MAX_MODULES;
    sceKernelGetModuleList(0xFF, modules, &count);
    // The list is newest first, print it in load order
    int index = 0;
    for (int i = (int)count - 1; i >= 0; i--) {
        if (was_there(modules[i]))
            continue;
        SceKernelModuleInfo info = { .size = sizeof(info) };
        sceKernelGetModuleInfo(modules[i], &info);
        PRINTF(1, "%s: new module %d is %s", what, ++index, info.module_name);
    }
    PRINTF(index, "%s: new modules", what);
}

static void partial_load(void) {
    PRINT("fill the process with threads", fill());
    print_call("load LOCATION_EXTENSION", sceSysmoduleLoadModule(SCE_SYSMODULE_LOCATION_EXTENSION));
    PRINT("load LOCATION_EXTENSION: it is loaded", sceSysmoduleIsLoaded(SCE_SYSMODULE_LOCATION_EXTENSION));
    free_one();
    print_call("load LOCATION_EXTENSION with room for one thread", sceSysmoduleLoadModule(SCE_SYSMODULE_LOCATION_EXTENSION));
    PRINT("load LOCATION_EXTENSION with room for one thread: it is loaded", sceSysmoduleIsLoaded(SCE_SYSMODULE_LOCATION_EXTENSION));
    free_all();
}

static void partial_unload(void) {
    // LOCATION_EXTENSION is loaded from the step before
    PRINT("fill the process with threads", fill());
    print_call("unload LOCATION_EXTENSION", sceSysmoduleUnloadModule(SCE_SYSMODULE_LOCATION_EXTENSION));
    PRINT("unload LOCATION_EXTENSION: it is loaded", sceSysmoduleIsLoaded(SCE_SYSMODULE_LOCATION_EXTENSION));
    free_one();
    print_call("unload LOCATION_EXTENSION with room for one thread", sceSysmoduleUnloadModule(SCE_SYSMODULE_LOCATION_EXTENSION));
    PRINT("unload LOCATION_EXTENSION with room for one thread: it is loaded", sceSysmoduleIsLoaded(SCE_SYSMODULE_LOCATION_EXTENSION));
    print_call("unload it again", sceSysmoduleUnloadModule(SCE_SYSMODULE_LOCATION_EXTENSION));
    free_all();
}

int main(void) {
    first_count = MAX_MODULES;
    sceKernelGetModuleList(0xFF, first_modules, &first_count);
    const SceUID id = sceKernelRegisterThreadEventHandler("starts", SCE_KERNEL_THREAD_ID_PROCESS_ALL_ID, SCE_KERNEL_THREAD_EVENT_TYPE_START, handler, NULL);
    PRINT("register the handler", test_created(id));

    partial_load();
    partial_unload();

    if (id >= 0)
        sceKernelUnregisterThreadEventHandler(id);
    test_exit();
    return 0;
}
