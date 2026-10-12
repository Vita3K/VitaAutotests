// Which sizes the sceKernelGet*Info calls take, how many bytes they write for each, and the size
// they leave in the struct
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

typedef int (*InfoFn)(SceUID id, void *info);

static unsigned char first[0x100] __attribute__((aligned(8)));
static unsigned char second[0x100] __attribute__((aligned(8)));

static int callback(int notify_id, int notify_count, int notify_arg, void *common) {
    (void)notify_id;
    (void)notify_count;
    (void)notify_arg;
    (void)common;
    return 0;
}

// Calls fn on two buffers filled with different bytes, so a written byte differs from its fill in
// at least one of them whatever its value
static void try_size(const char *name, InfoFn fn, SceUID id, SceSize size) {
    sceClibMemset(first, 0xCC, sizeof(first));
    sceClibMemset(second, 0x33, sizeof(second));
    sceClibMemcpy(first, &size, sizeof(size));
    sceClibMemcpy(second, &size, sizeof(size));
    const int ret = fn(id, first);
    fn(id, second);
    PRINTF(ret, "%s with a size of 0x%X", name, (unsigned int)size);
    if (ret < 0)
        return;

    int written = 0;
    for (int i = sizeof(SceSize); i < (int)sizeof(first); i++)
        if (first[i] != 0xCC || second[i] != 0x33)
            written = i + 1;
    PRINTF(written, "%s bytes written with a size of 0x%X", name, (unsigned int)size);
    SceSize size_after;
    sceClibMemcpy(&size_after, first, sizeof(size_after));
    PRINTF(size_after, "%s size left with a size of 0x%X", name, (unsigned int)size);
}

static void try_sizes(const char *name, InfoFn fn, SceUID id) {
    try_size(name, fn, id, 0);
    try_size(name, fn, id, 4);
    for (SceSize size = 0x20; size <= 0x60; size += 4)
        try_size(name, fn, id, size);
    try_size(name, fn, id, 0x100);
}

int main(void) {
    const SceUID mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    try_sizes("mutex", (InfoFn)sceKernelGetMutexInfo, mutex);

    const SceUID cb = sceKernelCreateCallback("callback", 0, callback, NULL);
    try_sizes("callback", (InfoFn)sceKernelGetCallbackInfo, cb);

    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 5, NULL);
    try_sizes("semaphore", (InfoFn)sceKernelGetSemaInfo, sema);

    const SceUID cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    try_sizes("condition variable", (InfoFn)sceKernelGetCondInfo, cond);

    const SceUID flag = sceKernelCreateEventFlag("flag", 0, 0x55, NULL);
    try_sizes("event flag", (InfoFn)sceKernelGetEventFlagInfo, flag);

    const SceUID rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    try_sizes("reader/writer lock", (InfoFn)sceKernelGetRWLockInfo, rwlock);

    sceKernelDeleteRWLock(rwlock);
    sceKernelDeleteEventFlag(flag);
    sceKernelDeleteCond(cond);
    sceKernelDeleteSema(sema);
    sceKernelDeleteCallback(cb);
    sceKernelDeleteMutex(mutex);

    test_exit();
    return 0;
}
