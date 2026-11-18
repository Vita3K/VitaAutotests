// A user MCRR p15, 0, r0, r1, c11 (0xEC410F0B) and whether the code word was patched afterwards.
// The call runs on the main thread, so a fault would end the process before the later lines.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

void ple_mcrr(void);
__asm__(".pushsection .text\n"
        ".arm\n"
        ".global ple_mcrr\n"
        ".type ple_mcrr, %function\n"
        "ple_mcrr:\n"
        "    mov r0, #0\n"
        "    mov r1, #0\n"
        "    .word 0xec410f0b\n"
        "    bx lr\n"
        ".popsection\n");

int main(void) {
    volatile SceUInt32 *word = (volatile SceUInt32 *)((SceUInt32)ple_mcrr + 8);
    PRINT("the word before the call is the mcrr", *word == 0xEC410F0B);
    PRINT("run the mcrr", 1);
    ple_mcrr();
    PRINT("the call returned", 1);
    PRINT("the word is now a nop", *word == 0xE320F000);
    ple_mcrr();
    PRINT("a second call returned", 1);
    test_exit();
    return 0;
}
