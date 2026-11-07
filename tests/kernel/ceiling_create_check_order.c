// Which error a mutex create gives when several of its arguments are wrong, for the checks of the
// option struct and the ceiling against the other checks. Each case creates one mutex from the main
// thread, at 0xA0, and prints the result. A created mutex is deleted at once. The cases pair one
// of a NULL option struct with the ceiling attribute, an option struct of size 12, or a ceiling of
// 0xC0, with one of an init count of 2 without the recursive attribute, an init count of -1, an
// attribute bit 0x8, or a NULL name.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define CEILING SCE_KERNEL_MUTEX_ATTR_CEILING

int main(void) {
    SceKernelMutexOptParam big = { .size = 12, .ceilingPriority = 0x80 };
    SceKernelMutexOptParam low = { .size = sizeof(low), .ceilingPriority = 0xC0 };
    static const struct {
        const char *label;
        int null_name;
        SceUInt attr;
        int init_count;
        int opt;
    } cases[] = {
        { "no option struct, init count 2", 0, CEILING, 2, 0 },
        { "no option struct, init count -1", 0, CEILING, -1, 0 },
        { "no option struct, attribute 0x8", 0, CEILING | 0x8, 0, 0 },
        { "no option struct, NULL name", 1, CEILING, 0, 0 },
        { "option size 12, init count 2", 0, CEILING, 2, 1 },
        { "option size 12, init count -1", 0, CEILING, -1, 1 },
        { "option size 12, attribute 0x8", 0, CEILING | 0x8, 0, 1 },
        { "option size 12, NULL name", 1, CEILING, 0, 1 },
        { "option size 12 without the ceiling attribute, init count 2", 0, 0, 2, 1 },
        { "ceiling 0xC0, init count 2", 0, CEILING, 2, 2 },
        { "ceiling 0xC0, init count -1", 0, CEILING, -1, 2 },
        { "ceiling 0xC0, attribute 0x8", 0, CEILING | 0x8, 0, 2 },
        { "ceiling 0xC0, NULL name", 1, CEILING, 0, 2 },
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        SceKernelMutexOptParam *const opts[] = { NULL, &big, &low };
        const SceUID mutex = sceKernelCreateMutex(cases[i].null_name ? NULL : "m", cases[i].attr,
                                                  cases[i].init_count, opts[cases[i].opt]);
        PRINT(cases[i].label, test_created(mutex));
        if (mutex >= 0)
            sceKernelDeleteMutex(mutex);
    }

    test_exit();
    return 0;
}
