// Where the entry address check of sceKernelCreateThread comes among the other argument checks.
// Each case creates a thread with a bad argument, once with the entry set to a word of this test
// (data, read and write but not executable) and once with a function of this test as a control.
// The result of each create is printed, and a thread that was made anyway is deleted. The cases:
// - the attr 0x1, which a game may not set
// - the priority above the highest game priority
// - a stack size of 0x100, below the smallest one
// - an option struct with the attr 0x2 and the user stack type 0x0E20D060, which a game may not use
//
// On a PS Vita TV on firmware 3.65 the data entry failed with 0x80020006
// (SCE_KERNEL_ERROR_ILLEGAL_ADDR) in all four cases. The code entry failed with 0x8002000E
// (SCE_KERNEL_ERROR_ILLEGAL_ATTR) for the attr 0x1, with 0x80028023
// (SCE_KERNEL_ERROR_ILLEGAL_PRIORITY) for the priority, with 0x8002000C
// (SCE_KERNEL_ERROR_ILLEGAL_TYPE) for the user stack type, and succeeded for the stack size 0x100.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define MEMTYPE_SYSTEM_RW 0x0E20D060

static volatile int data_word = 1;

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Creates a thread and deletes it if one was made, then prints the result
static void create(const char *what, SceKernelThreadEntry fn, int prio, SceSize stack,
    SceUInt32 attr, SceKernelThreadOptParam *opt) {
    const SceUID thread = sceKernelCreateThread("entry order", fn, prio, stack, attr, 0, opt);
    PRINTF(test_created(thread), "%s", what);
    if (thread >= 0)
        sceKernelDeleteThread(thread);
}

// Runs one case with the data entry and then with the code entry
static void both(const char *what, int prio, SceSize stack, SceUInt32 attr,
    SceKernelThreadOptParam *opt) {
    char text[128];
    sceClibSnprintf(text, sizeof(text), "%s, data entry", what);
    create(text, (SceKernelThreadEntry)&data_word, prio, stack, attr, opt);
    sceClibSnprintf(text, sizeof(text), "%s, code entry", what);
    create(text, entry, prio, stack, attr, opt);
}

int main(void) {
    const int prio = SCE_KERNEL_DEFAULT_PRIORITY_USER;

    both("attr 0x1", prio, 0x4000, 0x1, NULL);
    both("priority above the highest game priority", SCE_KERNEL_HIGHEST_PRIORITY_USER - 1, 0x4000,
        0, NULL);
    both("stack size 0x100", prio, 0x100, 0, NULL);

    SceKernelThreadOptParam opt = { .size = sizeof(opt), .attr = 0x2 };
    opt.uStackMemType = MEMTYPE_SYSTEM_RW;
    both("user stack type 0x0E20D060", prio, 0x4000, 0, &opt);

    test_exit();
    return 0;
}
