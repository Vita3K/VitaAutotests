// Whether sceKernelGetCallbackInfo checks the size in the struct before it looks the UID up, and
// what a failed call leaves in the struct. The struct sits at the start of a 0x100 byte buffer
// filled with 0xEE, so a call that writes past it shows. Each case sets the size, makes the call,
// and compares the whole buffer with a copy taken before. The cases:
// - size 0x45 with an unknown UID, UID 0, a semaphore, a deleted callback and a live callback
// - size 0x44 with an unknown UID
// - size 0xFFFFFFFF with an unknown UID
// - a NULL struct with an unknown UID
// Each case prints the result and, except the NULL one, whether the buffer is unchanged.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define UNKNOWN_UID 0x12345
#define BUFFER_SIZE 0x100

static unsigned char buffer[BUFFER_SIZE] __attribute__((aligned(8)));
static unsigned char before[BUFFER_SIZE];

static int callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    return 0;
}

typedef struct Case {
    const char *name;
    SceUID uid;
    SceSize size;
} Case;

int main(void) {
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    const SceUID deleted = sceKernelCreateCallback("deleted", 0, callback, NULL);
    sceKernelDeleteCallback(deleted);
    const SceUID live = sceKernelCreateCallback("live", 0, callback, NULL);

    const Case cases[] = {
        { "info with size 0x45 and an unknown UID", UNKNOWN_UID, 0x45 },
        { "info with size 0x45 and UID 0", 0, 0x45 },
        { "info with size 0x45 and a semaphore", sema, 0x45 },
        { "info with size 0x45 and a deleted callback", deleted, 0x45 },
        { "info with size 0x45 and a valid callback", live, 0x45 },
        { "info with size 0x44 and an unknown UID", UNKNOWN_UID, 0x44 },
        { "info with size 0xFFFFFFFF and an unknown UID", UNKNOWN_UID, 0xFFFFFFFF },
    };
    int results[sizeof(cases) / sizeof(cases[0])];
    int unchanged[sizeof(cases) / sizeof(cases[0])];
    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        sceClibMemset(buffer, 0xEE, sizeof(buffer));
        ((CallbackInfo *)buffer)->size = cases[i].size;
        sceClibMemcpy(before, buffer, sizeof(buffer));
        results[i] = sceKernelGetCallbackInfo(cases[i].uid, (SceKernelCallbackInfo *)buffer);
        unchanged[i] = sceClibMemcmp(before, buffer, sizeof(buffer)) == 0;
    }
    const int null_result = sceKernelGetCallbackInfo(UNKNOWN_UID, NULL);

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        PRINT(cases[i].name, results[i]);
        PRINTF(unchanged[i], "%s: the buffer after it is unchanged", cases[i].name);
    }
    PRINT("info with NULL and an unknown UID", null_result);

    sceKernelDeleteCallback(live);
    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
