// Module start and stop while the process has no room for another thread, so the thread that runs
// the start or stop function can't be made. The process is filled with threads that never start
// until a create fails, then the test's module and libfiber are loaded, started and stopped, and
// what each call returns, the status, whether the function ran and what is still loaded are
// printed. One thread is deleted after each case to show the same call then works.
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>

#include "probe_module/probe_module.h"
#include "test.h"

#define MODULE_PATH "app0:probe_module.suprx"
#define CAP 256

static SceUID threads[CAP];
static int thread_count;
static ModuleProbe probe;
static ModuleArgs args = { &probe };

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
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

// Deletes the last thread fill made
static void free_one(void) {
    if (thread_count > 0)
        sceKernelDeleteThread(threads[--thread_count]);
}

static void free_all(void) {
    while (thread_count > 0)
        free_one();
}

static void load_start(void) {
    int status = 0xDEAD;
    probe.runs = 0;
    PRINT("fill the process with threads", fill());
    const SceUID module = sceKernelLoadStartModule(MODULE_PATH, sizeof(args), &args, 0, NULL, &status);
    PRINT("load and start", module);
    PRINT("load and start: the status", status);
    PRINT("load and start: runs of the start function", probe.runs);
    free_one();
    status = 0xDEAD;
    const SceUID again = sceKernelLoadStartModule(MODULE_PATH, sizeof(args), &args, 0, NULL, &status);
    PRINT("load and start with room for one thread", test_created(again));
    PRINT("load and start with room for one thread: the status", status);
    PRINT("load and start with room for one thread: runs of the start function", probe.runs);
    if (again >= 0)
        sceKernelStopUnloadModule(again, sizeof(args), &args, 0, NULL, NULL);
    free_all();
}

static void start(void) {
    probe.runs = 0;
    const SceUID module = sceKernelLoadModule(MODULE_PATH, 0, NULL);
    PRINT("load", test_created(module));
    PRINT("fill the process with threads", fill());
    int status = 0xDEAD;
    PRINT("start", sceKernelStartModule(module, sizeof(args), &args, 0, NULL, &status));
    PRINT("start: the status", status);
    PRINT("start: runs of the start function", probe.runs);
    status = 0xDEAD;
    PRINT("start it again", sceKernelStartModule(module, sizeof(args), &args, 0, NULL, &status));
    PRINT("start it again: the status", status);
    free_one();
    status = 0xDEAD;
    PRINT("start it with room for one thread", sceKernelStartModule(module, sizeof(args), &args, 0, NULL, &status));
    PRINT("start it with room for one thread: the status", status);
    PRINT("start it with room for one thread: runs of the start function", probe.runs);
    sceKernelStopUnloadModule(module, sizeof(args), &args, 0, NULL, NULL);
    free_all();
}

static void stop(void) {
    probe.runs = 0;
    int status = 0xDEAD;
    const SceUID module = sceKernelLoadStartModule(MODULE_PATH, sizeof(args), &args, 0, NULL, &status);
    PRINT("load and start before the fill", test_created(module));
    PRINT("fill the process with threads", fill());
    status = 0xDEAD;
    PRINT("stop", sceKernelStopModule(module, sizeof(args), &args, 0, NULL, &status));
    PRINT("stop: the status", status);
    PRINT("stop: runs of both functions", probe.runs);
    status = 0xDEAD;
    PRINT("stop and unload", sceKernelStopUnloadModule(module, sizeof(args), &args, 0, NULL, &status));
    PRINT("stop and unload: the status", status);
    PRINT("stop and unload: runs of both functions", probe.runs);
    free_one();
    status = 0xDEAD;
    PRINT("stop and unload with room for one thread", sceKernelStopUnloadModule(module, sizeof(args), &args, 0, NULL, &status));
    PRINT("stop and unload with room for one thread: the status", status);
    PRINT("stop and unload with room for one thread: runs of both functions", probe.runs);
    free_all();
}

static void sysmodule(void) {
    PRINT("libfiber is loaded before", sceSysmoduleIsLoaded(SCE_SYSMODULE_FIBER));
    PRINT("fill the process with threads", fill());
    PRINT("load libfiber", sceSysmoduleLoadModule(SCE_SYSMODULE_FIBER));
    PRINT("load libfiber: it is loaded", sceSysmoduleIsLoaded(SCE_SYSMODULE_FIBER));
    PRINT("load libfiber again", sceSysmoduleLoadModule(SCE_SYSMODULE_FIBER));
    PRINT("load libfiber again: it is loaded", sceSysmoduleIsLoaded(SCE_SYSMODULE_FIBER));
    PRINT("unload libfiber", sceSysmoduleUnloadModule(SCE_SYSMODULE_FIBER));
    PRINT("unload libfiber: it is loaded", sceSysmoduleIsLoaded(SCE_SYSMODULE_FIBER));
    free_one();
    PRINT("load libfiber with room for one thread", sceSysmoduleLoadModule(SCE_SYSMODULE_FIBER));
    PRINT("load libfiber with room for one thread: it is loaded", sceSysmoduleIsLoaded(SCE_SYSMODULE_FIBER));
    PRINT("fill the process with threads again", fill());
    PRINT("unload libfiber with no room", sceSysmoduleUnloadModule(SCE_SYSMODULE_FIBER));
    PRINT("unload libfiber with no room: it is loaded", sceSysmoduleIsLoaded(SCE_SYSMODULE_FIBER));
    free_one();
    PRINT("unload libfiber with room for one thread", sceSysmoduleUnloadModule(SCE_SYSMODULE_FIBER));
    PRINT("unload libfiber with room for one thread: it is loaded", sceSysmoduleIsLoaded(SCE_SYSMODULE_FIBER));
    free_all();
}

int main(void) {
    load_start();
    start();
    stop();
    sysmodule();
    test_exit();
    return 0;
}
