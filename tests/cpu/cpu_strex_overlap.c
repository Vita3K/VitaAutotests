// Exclusive stores whose status register is also their value or base register. ARMv7 leaves these
// encodings UNPREDICTABLE, so this records what the Vita's CPU does with them, with the exclusive
// monitor open by a load exclusive of the same size and closed by CLREX. Each case starts with r0 =
// 0x0A0B0C0D, r1 = 0x12345678, r3 = 0x33333333, r2 = the address of two words 0xA5A5A5A5 and
// 0x5A5A5A5A, runs the load exclusive or CLREX, then the case's store, then a plain exclusive store
// of the same size of 0x5A with a separate status register and no new load exclusive, which succeeds
// only if the case left the monitor open. A store with a separate status register is one control,
// a case that runs nothing between the load exclusive and the plain store is the other, one of each
// size.
//
// The test prints r0 to r3 and both words after the case, then the status of the plain store and
// both words after it. The address in r2 prints as kind 2, any other value as kind 3 and itself.
//
// On a PS Vita TV on firmware 3.65, every case here works like an exclusive store with a separate
// status register: with the monitor open it stores r1, or r0 and r1 for STREXD, as they were before
// the instruction, to the address r2 held, then writes 0 to the status register. With the monitor
// closed it stores nothing and writes 1. Either way the monitor is closed after. The cases left out
// raise an undefined instruction exception there, which ends the process: a Thumb STREXD whose status
// register is its second value register (strexd r1, r0, r1, [r2]), and an ARM STREXD whose status
// register is either value register (strexd r0, r0, r1, [r2] and strexd r1, r0, r1, [r2]).
#include "test.h"

static unsigned int words[2] __attribute__((aligned(8)));

// The code around a case, with setup the load exclusive or CLREX, enc the case's instruction and check
// the plain exclusive store of the same size
#define CASE_BODY(setup, enc, check)                \
    "    push {r4-r11, lr}\n"                       \
    "    mov r4, r1\n"                              \
    "    mov r6, r0\n"                              \
    "    mov r2, r0\n"                              \
    "    movw r0, #0x0c0d\n"                        \
    "    movt r0, #0x0a0b\n"                        \
    "    movw r1, #0x5678\n"                        \
    "    movt r1, #0x1234\n"                        \
    "    movw r3, #0x3333\n"                        \
    "    movt r3, #0x3333\n"                        \
    "    " setup "\n"                               \
    "    " enc "\n"                                 \
    "    str r0, [r4]\n"                            \
    "    str r1, [r4, #4]\n"                        \
    "    str r2, [r4, #8]\n"                        \
    "    str r3, [r4, #12]\n"                       \
    "    ldr r5, [r6]\n"                            \
    "    str r5, [r4, #16]\n"                       \
    "    ldr r5, [r6, #4]\n"                        \
    "    str r5, [r4, #20]\n"                       \
    "    mov r8, #0x5a\n"                           \
    "    mov r9, #0x5a\n"                           \
    "    " check "\n"                               \
    "    str r3, [r4, #24]\n"                       \
    "    ldr r5, [r6]\n"                            \
    "    str r5, [r4, #28]\n"                       \
    "    ldr r5, [r6, #4]\n"                        \
    "    str r5, [r4, #32]\n"                       \
    "    pop {r4-r11, pc}\n"

// The plain exclusive stores of each size
#define CHECK_W "strex r3, r8, [r6]"
#define CHECK_B "strexb r3, r8, [r6]"
#define CHECK_H "strexh r3, r8, [r6]"
#define CHECK_D "strexd r3, r8, r9, [r6]"

#define THUMB_CASE(name, setup, enc, check)         \
    void name(unsigned int *word, unsigned int *out); \
    __asm__(".pushsection .text\n"                \
            ".syntax unified\n"                     \
            ".thumb\n"                              \
            ".thumb_func\n"                         \
            ".global " #name "\n"                   \
            ".type " #name ", %function\n" #name    \
            ":\n" CASE_BODY(setup, ".inst.w " #enc, check) \
            ".popsection\n");

#define ARM_CASE(name, setup, enc, check)           \
    void name(unsigned int *word, unsigned int *out); \
    __asm__(".pushsection .text\n"                \
            ".syntax unified\n"                     \
            ".arm\n"                                \
            ".global " #name "\n"                   \
            ".type " #name ", %function\n" #name    \
            ":\n" CASE_BODY(setup, ".inst " #enc, check) \
            ".thumb\n"                             \
            ".popsection\n");

// strex r1, r1, [r2], the one a game uses
THUMB_CASE(t_rd_rt_open, "ldrex r3, [r2]", 0xe8421100, CHECK_W)
THUMB_CASE(t_rd_rt_closed, "clrex", 0xe8421100, CHECK_W)
// strex r2, r1, [r2]
THUMB_CASE(t_rd_rn_open, "ldrex r3, [r2]", 0xe8421200, CHECK_W)
THUMB_CASE(t_rd_rn_closed, "clrex", 0xe8421200, CHECK_W)
// strexb r1, r1, [r2]
THUMB_CASE(t_b_rd_rt_open, "ldrexb r3, [r2]", 0xe8c21f41, CHECK_B)
THUMB_CASE(t_b_rd_rt_closed, "clrex", 0xe8c21f41, CHECK_B)
// strexb r2, r1, [r2]
THUMB_CASE(t_b_rd_rn_open, "ldrexb r3, [r2]", 0xe8c21f42, CHECK_B)
// strexh r1, r1, [r2]
THUMB_CASE(t_h_rd_rt_open, "ldrexh r3, [r2]", 0xe8c21f51, CHECK_H)
THUMB_CASE(t_h_rd_rt_closed, "clrex", 0xe8c21f51, CHECK_H)
// strexh r2, r1, [r2]
THUMB_CASE(t_h_rd_rn_open, "ldrexh r3, [r2]", 0xe8c21f52, CHECK_H)
// strexd r0, r0, r1, [r2]
THUMB_CASE(t_d_rd_rt_open, "ldrexd r8, r9, [r2]", 0xe8c20170, CHECK_D)
THUMB_CASE(t_d_rd_rt_closed, "clrex", 0xe8c20170, CHECK_D)
// strexd r2, r0, r1, [r2]
THUMB_CASE(t_d_rd_rn_open, "ldrexd r8, r9, [r2]", 0xe8c20172, CHECK_D)
// strex r3, r1, [r2], the control
THUMB_CASE(t_control_open, "ldrex r3, [r2]", 0xe8421300, CHECK_W)
THUMB_CASE(t_control_closed, "clrex", 0xe8421300, CHECK_W)
// Nothing between the load exclusive and the plain store, nop.w and nop, the other control
THUMB_CASE(t_nothing_w, "ldrex r3, [r2]", 0xf3af8000, CHECK_W)
THUMB_CASE(t_nothing_b, "ldrexb r3, [r2]", 0xf3af8000, CHECK_B)
THUMB_CASE(t_nothing_h, "ldrexh r3, [r2]", 0xf3af8000, CHECK_H)
THUMB_CASE(t_nothing_d, "ldrexd r10, r11, [r2]", 0xf3af8000, CHECK_D)
ARM_CASE(a_nothing_w, "ldrex r3, [r2]", 0xe320f000, CHECK_W)
ARM_CASE(a_nothing_b, "ldrexb r3, [r2]", 0xe320f000, CHECK_B)
ARM_CASE(a_nothing_h, "ldrexh r3, [r2]", 0xe320f000, CHECK_H)
ARM_CASE(a_nothing_d, "ldrexd r10, r11, [r2]", 0xe320f000, CHECK_D)
// strex r1, r1, [r2]
ARM_CASE(a_rd_rt_open, "ldrex r3, [r2]", 0xe1821f91, CHECK_W)
ARM_CASE(a_rd_rt_closed, "clrex", 0xe1821f91, CHECK_W)
// strex r2, r1, [r2]
ARM_CASE(a_rd_rn_open, "ldrex r3, [r2]", 0xe1822f91, CHECK_W)
// strex r2, r1, [r2]
ARM_CASE(a_rd_rn_closed, "clrex", 0xe1822f91, CHECK_W)
// strexb r1, r1, [r2]
ARM_CASE(a_b_rd_rt_open, "ldrexb r3, [r2]", 0xe1c21f91, CHECK_B)
// strexb r2, r1, [r2]
ARM_CASE(a_b_rd_rn_open, "ldrexb r3, [r2]", 0xe1c22f91, CHECK_B)
// strexh r1, r1, [r2]
ARM_CASE(a_h_rd_rt_open, "ldrexh r3, [r2]", 0xe1e21f91, CHECK_H)
// strexh r2, r1, [r2]
ARM_CASE(a_h_rd_rn_open, "ldrexh r3, [r2]", 0xe1e22f91, CHECK_H)
// strexd r2, r0, r1, [r2]
ARM_CASE(a_d_rd_rn_open, "ldrexd r8, r9, [r2]", 0xe1a22f90, CHECK_D)
// strex r3, r1, [r2], the control
ARM_CASE(a_control_open, "ldrex r3, [r2]", 0xe1823f91, CHECK_W)

static void print_value(const char *name, const char *what, unsigned int value) {
    if (value == (unsigned int)words) {
        PRINTF(2, "%s: %s: kind", name, what);
        return;
    }
    PRINTF(3, "%s: %s: kind", name, what);
    PRINTF(value, "%s: %s", name, what);
}

static void run(const char *name, void (*fn)(unsigned int *, unsigned int *)) {
    unsigned int out[9];
    words[0] = 0xA5A5A5A5U;
    words[1] = 0x5A5A5A5AU;
    fn(words, out);
    print_value(name, "r0", out[0]);
    print_value(name, "r1", out[1]);
    print_value(name, "r2", out[2]);
    print_value(name, "r3", out[3]);
    print_value(name, "first word", out[4]);
    print_value(name, "second word", out[5]);
    print_value(name, "status of the plain store after", out[6]);
    print_value(name, "first word after the plain store", out[7]);
    print_value(name, "second word after the plain store", out[8]);
}

int main(void) {
    run("thumb strex r1, r1 with the monitor open", t_rd_rt_open);
    run("thumb strex r1, r1 with the monitor closed", t_rd_rt_closed);
    run("thumb strex r2, r1, [r2] with the monitor open", t_rd_rn_open);
    run("thumb strex r2, r1, [r2] with the monitor closed", t_rd_rn_closed);
    run("thumb strexb r1, r1 with the monitor open", t_b_rd_rt_open);
    run("thumb strexb r1, r1 with the monitor closed", t_b_rd_rt_closed);
    run("thumb strexb r2, r1, [r2] with the monitor open", t_b_rd_rn_open);
    run("thumb strexh r1, r1 with the monitor open", t_h_rd_rt_open);
    run("thumb strexh r1, r1 with the monitor closed", t_h_rd_rt_closed);
    run("thumb strexh r2, r1, [r2] with the monitor open", t_h_rd_rn_open);
    run("thumb strexd r0, r0, r1 with the monitor open", t_d_rd_rt_open);
    run("thumb strexd r0, r0, r1 with the monitor closed", t_d_rd_rt_closed);
    run("thumb strexd r2, r0, r1, [r2] with the monitor open", t_d_rd_rn_open);
    run("thumb strex r3, r1 with the monitor open", t_control_open);
    run("thumb strex r3, r1 with the monitor closed", t_control_closed);
    run("thumb nothing after ldrex", t_nothing_w);
    run("thumb nothing after ldrexb", t_nothing_b);
    run("thumb nothing after ldrexh", t_nothing_h);
    run("thumb nothing after ldrexd", t_nothing_d);
    run("arm strex r1, r1 with the monitor open", a_rd_rt_open);
    run("arm strex r1, r1 with the monitor closed", a_rd_rt_closed);
    run("arm strex r2, r1, [r2] with the monitor open", a_rd_rn_open);
    run("arm strex r2, r1, [r2] with the monitor closed", a_rd_rn_closed);
    run("arm strexb r1, r1 with the monitor open", a_b_rd_rt_open);
    run("arm strexb r2, r1, [r2] with the monitor open", a_b_rd_rn_open);
    run("arm strexh r1, r1 with the monitor open", a_h_rd_rt_open);
    run("arm strexh r2, r1, [r2] with the monitor open", a_h_rd_rn_open);
    run("arm strexd r2, r0, r1, [r2] with the monitor open", a_d_rd_rn_open);
    run("arm strex r3, r1 with the monitor open", a_control_open);
    run("arm nothing after ldrex", a_nothing_w);
    run("arm nothing after ldrexb", a_nothing_b);
    run("arm nothing after ldrexh", a_nothing_h);
    run("arm nothing after ldrexd", a_nothing_d);
    test_exit();
    return 0;
}
