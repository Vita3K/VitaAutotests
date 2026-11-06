// A WFI that a thread runs in user mode, from ARM code and from Thumb code. Each thread entry puts
// 0x1234 in r0, runs WFI, then returns 1, so its exit status shows whether it went on after the WFI
// (1) or ended there (0x1234). Each case prints the result of the start, of the wait for the
// thread's end, and its exit status.
//
// On a PS Vita TV on firmware 3.65 both threads go on after the WFI and exit with 1.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

// The entries: r0 = 0x1234, WFI, then return 1
int wfi_arm_entry(SceSize args, void *argp);
int wfi_thumb_entry(SceSize args, void *argp);
__asm__(".pushsection .text\n"
        ".syntax unified\n"
        ".arm\n"
        ".global wfi_arm_entry\n"
        ".type wfi_arm_entry, %function\n"
        "wfi_arm_entry:\n"
        "    movw r0, #0x1234\n"
        "    wfi\n"
        "    mov r0, #1\n"
        "    bx lr\n"
        ".thumb\n"
        ".thumb_func\n"
        ".global wfi_thumb_entry\n"
        ".type wfi_thumb_entry, %function\n"
        "wfi_thumb_entry:\n"
        "    movw r0, #0x1234\n"
        "    wfi\n"
        "    movs r0, #1\n"
        "    bx lr\n"
        ".popsection\n");

static void run_case(const char *name, SceKernelThreadEntry entry) {
    const SceUID thread = sceKernelCreateThread(name, entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    PRINTF(sceKernelStartThread(thread, 0, NULL), "%s: start", name);
    int status = 0xDEAD;
    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelWaitThreadEnd(thread, &status, &timeout), "%s: the wait for its end", name);
    PRINTF(status, "%s: its exit status", name);
    sceKernelDeleteThread(thread);
}

int main(void) {
    run_case("ARM", wfi_arm_entry);
    run_case("Thumb", wfi_thumb_entry);
    test_exit();
    return 0;
}
