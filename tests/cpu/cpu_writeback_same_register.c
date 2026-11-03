// Loads and stores whose base register is also the register they load or store, with a writeback.
// ARMv7 leaves these encodings UNPREDICTABLE, so this records what the Vita's CPU does with them.
// Each case runs one instruction, with r0 as both registers, on a buffer of eight words whose base
// starts at the fifth. The test then prints what r0 holds and every word that changed.
//
// A value is printed as a kind and a number, never as an address: kind 1 is the starting pattern of
// a buffer word and the number its index, kind 2 is the base address plus the number, kind 3 is the
// value itself.
//
// On a PS Vita TV on firmware 3.65, every case does what the instruction would do with two separate
// registers, in order. A store writes the register's value from before the instruction, the base, to
// the address it computes, then r0 gets the written back address. A load leaves r0 with the loaded
// value, and the written back address is lost. Thumb and ARM behave the same.
#include "test.h"

#define WORDS 8
#define BASE 4
#define PATTERN(i) (0xA0A00000U + (i))

static unsigned int buf[WORDS] __attribute__((aligned(8)));

// A Thumb case: r0 is the base on entry, runs the 32-bit Thumb instruction enc, returns r0
#define THUMB_CASE(name, enc)                       \
    unsigned int name(unsigned int *base);          \
    __asm__(".pushsection .text\n"                \
            ".syntax unified\n"                     \
            ".thumb\n"                              \
            ".thumb_func\n"                         \
            ".global " #name "\n"                   \
            ".type " #name ", %function\n" #name    \
            ":\n"                                   \
            "    .inst.w " #enc "\n"                \
            "    bx lr\n"                           \
            ".popsection\n");

// An ARM case: the same with the 32-bit ARM instruction enc
#define ARM_CASE(name, enc)                         \
    unsigned int name(unsigned int *base);          \
    __asm__(".pushsection .text\n"                \
            ".syntax unified\n"                     \
            ".arm\n"                                \
            ".global " #name "\n"                   \
            ".type " #name ", %function\n" #name    \
            ":\n"                                   \
            "    .inst " #enc "\n"                  \
            "    bx lr\n"                           \
            ".thumb\n"                             \
            ".popsection\n");

// str r0, [r0], #12, the one a game uses
THUMB_CASE(t_str_post_add, 0xf8400b0c)
// str r0, [r0], #-4
THUMB_CASE(t_str_post_sub, 0xf8400904)
// str r0, [r0, #4]!
THUMB_CASE(t_str_pre_add, 0xf8400f04)
// str r0, [r0, #-4]!
THUMB_CASE(t_str_pre_sub, 0xf8400d04)
// strb r0, [r0], #1
THUMB_CASE(t_strb_post_add, 0xf8000b01)
// strh r0, [r0], #2
THUMB_CASE(t_strh_post_add, 0xf8200b02)
// ldr r0, [r0], #4
THUMB_CASE(t_ldr_post_add, 0xf8500b04)
// ldr r0, [r0, #4]!
THUMB_CASE(t_ldr_pre_add, 0xf8500f04)
// ldrb r0, [r0], #1
THUMB_CASE(t_ldrb_post_add, 0xf8100b01)
// ldrh r0, [r0], #2
THUMB_CASE(t_ldrh_post_add, 0xf8300b02)
// str r0, [r0], #12
ARM_CASE(a_str_post_add, 0xe480000c)
// str r0, [r0, #4]!
ARM_CASE(a_str_pre_add, 0xe5a00004)
// strb r0, [r0], #1
ARM_CASE(a_strb_post_add, 0xe4c00001)
// strh r0, [r0], #2
ARM_CASE(a_strh_post_add, 0xe0c000b2)
// ldr r0, [r0], #4
ARM_CASE(a_ldr_post_add, 0xe4900004)
// ldr r0, [r0, #4]!
ARM_CASE(a_ldr_pre_add, 0xe5b00004)
// ldrb r0, [r0], #1
ARM_CASE(a_ldrb_post_add, 0xe4d00001)
// ldrh r0, [r0], #2
ARM_CASE(a_ldrh_post_add, 0xe0d000b2)

static void print_value(const char *name, const char *what, unsigned int value, unsigned int base) {
    for (int i = 0; i < WORDS; i++) {
        if (value == PATTERN(i)) {
            PRINTF(1, "%s: %s: kind", name, what);
            PRINTF(i, "%s: %s: number", name, what);
            return;
        }
    }
    const int distance = (int)(value - base);
    if (distance >= -64 && distance <= 64) {
        PRINTF(2, "%s: %s: kind", name, what);
        PRINTF(distance, "%s: %s: number", name, what);
        return;
    }
    PRINTF(3, "%s: %s: kind", name, what);
    PRINTF(value, "%s: %s: number", name, what);
}

// A byte or halfword store of size bytes leaves the rest of its word alone, so a changed word whose
// upper bytes keep the pattern prints its low part as a distance from the base's low part
static void print_word(const char *name, int index, unsigned int base, int size) {
    char what[16];
    const unsigned int word = buf[index];
    sceClibSnprintf(what, sizeof(what), "word %d", index);
    if (size == 1 && (word & 0xFFFFFF00U) == (PATTERN(index) & 0xFFFFFF00U)) {
        PRINTF((int)((word & 0xFF) - (base & 0xFF)), "%s: %s: low byte minus the base's", name, what);
        return;
    }
    if (size == 2 && (word & 0xFFFF0000U) == (PATTERN(index) & 0xFFFF0000U)) {
        PRINTF((int)((word & 0xFFFF) - (base & 0xFFFF)), "%s: %s: low halfword minus the base's", name, what);
        return;
    }
    print_value(name, what, word, base);
}

// Runs fn, whose access is size bytes
static void run(const char *name, unsigned int (*fn)(unsigned int *), int size) {
    for (int i = 0; i < WORDS; i++)
        buf[i] = PATTERN(i);
    unsigned int *const base_ptr = &buf[BASE];
    const unsigned int base = (unsigned int)base_ptr;
    const unsigned int r0 = fn(base_ptr);
    print_value(name, "r0", r0, base);
    int changed = 0;
    for (int i = 0; i < WORDS; i++) {
        if (buf[i] != PATTERN(i)) {
            changed++;
            print_word(name, i, base, size);
        }
    }
    PRINTF(changed, "%s: words changed", name);
}

int main(void) {
    run("thumb str post-indexed +12", t_str_post_add, 4);
    run("thumb str post-indexed -4", t_str_post_sub, 4);
    run("thumb str pre-indexed +4 with writeback", t_str_pre_add, 4);
    run("thumb str pre-indexed -4 with writeback", t_str_pre_sub, 4);
    run("thumb strb post-indexed +1", t_strb_post_add, 1);
    run("thumb strh post-indexed +2", t_strh_post_add, 2);
    run("thumb ldr post-indexed +4", t_ldr_post_add, 4);
    run("thumb ldr pre-indexed +4 with writeback", t_ldr_pre_add, 4);
    run("thumb ldrb post-indexed +1", t_ldrb_post_add, 1);
    run("thumb ldrh post-indexed +2", t_ldrh_post_add, 2);
    run("arm str post-indexed +12", a_str_post_add, 4);
    run("arm str pre-indexed +4 with writeback", a_str_pre_add, 4);
    run("arm strb post-indexed +1", a_strb_post_add, 1);
    run("arm strh post-indexed +2", a_strh_post_add, 2);
    run("arm ldr post-indexed +4", a_ldr_post_add, 4);
    run("arm ldr pre-indexed +4 with writeback", a_ldr_pre_add, 4);
    run("arm ldrb post-indexed +1", a_ldrb_post_add, 1);
    run("arm ldrh post-indexed +2", a_ldrh_post_add, 2);
    test_exit();
    return 0;
}
