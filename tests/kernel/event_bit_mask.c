// Which simple event bits a game may set, clear and wait for. The low bits are system events
// (input, output, create, delete and so on), and only some bits are meant for games. For each bit,
// a fresh event is set to it, and the result and the pattern after it are printed.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    char label[64];
    SceUInt32 pattern;

    const SceUID fresh = sceKernelCreateSimpleEvent("fresh", 0, 0, NULL);
    pattern = 0xDEAD;
    sceKernelGetEventPattern(fresh, &pattern);
    PRINT("the pattern of a new event", pattern);

    for (int bit = 0; bit < 32; bit++) {
        const SceUInt32 mask = 1u << bit;
        const SceUID event = sceKernelCreateSimpleEvent("bit", 0, 0, NULL);
        sceClibSnprintf(label, sizeof(label), "set bit %d", bit);
        PRINT(label, sceKernelSetEvent(event, mask, 0));
        pattern = 0xDEAD;
        sceKernelGetEventPattern(event, &pattern);
        sceClibSnprintf(label, sizeof(label), "the pattern after setting bit %d", bit);
        PRINT(label, pattern);
        sceKernelDeleteSimpleEvent(event);
    }

    // The initial pattern and clear go through the same check
    PRINT("create with the low byte set", test_created(sceKernelCreateSimpleEvent("low", 0, 0xFF, NULL)));
    PRINT("create with the high byte set", test_created(sceKernelCreateSimpleEvent("high", 0, 0xFF000000, NULL)));
    PRINT("clear keeping the low byte", sceKernelClearEvent(fresh, 0xFF));
    PRINT("clear keeping every bit", sceKernelClearEvent(fresh, 0xFFFFFFFF));

    test_exit();
    return 0;
}
