// Which attributes and option sizes a lightweight mutex create takes, and which error it gives when
// several of its arguments are wrong. Each case creates one lightweight mutex from the main thread
// in a fresh work area and prints the result. A created mutex is deleted at once. The cases:
// - the attributes 0x4, 0x8000, 0x2000 and 0x2002 alone
// - option struct sizes of 0, 4 and 8
// - pairs of an attribute 0x4, an option struct of size 8, an init count of -1, a NULL name, a NULL
//   work area and a work area 4 bytes off its alignment
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

enum { NAME_OK, NAME_NULL };
enum { WORK_OK, WORK_NULL, WORK_MISALIGNED };
enum { OPT_NONE, OPT_0, OPT_4, OPT_8 };

static const struct {
    const char *label;
    int name, work;
    unsigned int attr;
    int init_count, opt;
} cases[] = {
    { "attribute 0x4", NAME_OK, WORK_OK, 0x4, 0, OPT_NONE },
    { "attribute 0x8000", NAME_OK, WORK_OK, 0x8000, 0, OPT_NONE },
    { "attribute 0x2000", NAME_OK, WORK_OK, 0x2000, 0, OPT_NONE },
    { "attribute 0x2002", NAME_OK, WORK_OK, 0x2002, 0, OPT_NONE },
    { "option size 0", NAME_OK, WORK_OK, 0, 0, OPT_0 },
    { "option size 4", NAME_OK, WORK_OK, 0, 0, OPT_4 },
    { "option size 8", NAME_OK, WORK_OK, 0, 0, OPT_8 },
    { "attribute 0x4, init count -1", NAME_OK, WORK_OK, 0x4, -1, OPT_NONE },
    { "attribute 0x4, option size 8", NAME_OK, WORK_OK, 0x4, 0, OPT_8 },
    { "attribute 0x4, NULL name", NAME_NULL, WORK_OK, 0x4, 0, OPT_NONE },
    { "attribute 0x4, NULL work area", NAME_OK, WORK_NULL, 0x4, 0, OPT_NONE },
    { "attribute 0x4, misaligned work area", NAME_OK, WORK_MISALIGNED, 0x4, 0, OPT_NONE },
    { "option size 8, init count -1", NAME_OK, WORK_OK, 0, -1, OPT_8 },
    { "option size 8, NULL name", NAME_NULL, WORK_OK, 0, 0, OPT_8 },
    { "init count -1, NULL name", NAME_NULL, WORK_OK, 0, -1, OPT_NONE },
    { "init count -1, NULL work area", NAME_OK, WORK_NULL, 0, -1, OPT_NONE },
    { "NULL name, NULL work area", NAME_NULL, WORK_NULL, 0, 0, OPT_NONE },
    { "NULL name, misaligned work area", NAME_NULL, WORK_MISALIGNED, 0, 0, OPT_NONE },
};

static SceKernelLwMutexWork works[sizeof(cases) / sizeof(cases[0]) + 1] __attribute__((aligned(8)));

int main(void) {
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        SceKernelLwMutexOptParam opts[] = { { 0 }, { 0 }, { 4 }, { 8 } };
        SceKernelLwMutexWork *work = &works[i];
        if (cases[i].work == WORK_NULL)
            work = NULL;
        else if (cases[i].work == WORK_MISALIGNED)
            work = (SceKernelLwMutexWork *)((char *)work + 4);
        const int result = sceKernelCreateLwMutex(work, cases[i].name == NAME_NULL ? NULL : "m", cases[i].attr,
                                                  cases[i].init_count, cases[i].opt == OPT_NONE ? NULL : &opts[cases[i].opt]);
        PRINT(cases[i].label, result);
        if (result >= 0)
            sceKernelDeleteLwMutex(work);
    }

    test_exit();
    return 0;
}
