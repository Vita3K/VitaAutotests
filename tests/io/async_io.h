// Async file calls and a few helpers shared by the async io tests. The calls return the UID of an
// event, which a game waits on with the event calls.
#pragma once

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include "test.h"

// The user's own declarations, since the headers lack these. The last argument is the optional
// 0x18 byte parameter block.
SceUID sceIoOpenAsync(const char *file, int flags, SceMode mode, void *param);
SceUID sceIoCloseAsync(SceUID fd, void *param);
SceUID sceIoReadAsync(SceUID fd, void *data, SceSize size, void *param);
SceUID sceIoWriteAsync(SceUID fd, const void *data, SceSize size, void *param);
int sceIoComplete(SceUID uid);

#define ASYNC_WAIT_US 5000000

// The result of one async request
typedef struct AsyncResult {
    int wait;
    unsigned pattern;
    unsigned long long user_data;
} AsyncResult;

// Waits for the request's event, taking its result
static inline AsyncResult async_wait(SceUID uid) {
    AsyncResult result = { 0, 0, 0 };
    SceUInt32 timeout = ASYNC_WAIT_US;
    result.wait = sceKernelWaitEvent(uid, 1, &result.pattern, &result.user_data, &timeout);
    return result;
}

// Waits until the request's event has its result set, without taking it
static inline void async_wait_set(SceUID uid) {
    SceUInt32 pattern = 0;
    TEST_WAIT_FOR(sceKernelGetEventPattern(uid, &pattern) < 0 || (pattern & 1));
}

// Prints the pieces of a result under the label
static inline void async_print(const char *label, AsyncResult result) {
    PRINTF(result.wait, "%s wait", label);
    PRINTF(result.pattern, "%s pattern", label);
    PRINTF((unsigned)result.user_data, "%s result", label);
    PRINTF((unsigned)(result.user_data >> 32), "%s result high word", label);
}

// Makes a file with the given bytes
static inline void async_make_file(const char *path, const void *data, int size) {
    sceIoRemove(path);
    const SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    sceIoWrite(fd, data, size);
    sceIoClose(fd);
}
