// Which kernel calls that never switch threads still change d0 to d31 or FPSCR for the caller.
// The kernel runs on the caller's live VFP registers, so a call that uses a NEON helper like
// memset or memcpy leaves what the helper put there. syscall_vfp_clobber shows the calls that
// block, this test looks at calls that don't. Each case runs on one thread with no other thread
// able to run. One asm block loads markers into d0 to d31 and a known FPSCR, calls the import,
// stores the registers and reads FPSCR again, so no compiler code touches them in between.
//
// The cases are a message pipe try send of 4 KiB, a try receive of 4 KiB, a try send of 64 bytes
// (each with no waiter), sceKernelGetThreadInfo, sceKernelGetSemaInfo, sceKernelGetProcessTimeWide,
// sceKernelGetSystemTimeWide, a sceKernelPollSema that succeeds, sceKernelGetCallbackInfo, a 64 KiB
// sceKernelAllocMemBlock and the sceKernelFreeMemBlock of it. Each case uses a fresh object and
// deletes it. For each call it prints which d registers changed, one bit each, and whether FPSCR
// changed. The calls that return a result with no UID in it print it too.
//
// On a PS Vita TV on firmware 3.65 every call that prints a result returns 0 and FPSCR is kept in
// all 11 cases. The three message pipe calls, both time calls, the poll and sceKernelFreeMemBlock
// keep all 32 d registers. sceKernelGetThreadInfo changes d0, d1 and d16 (0x00010003).
// sceKernelGetSemaInfo, sceKernelGetCallbackInfo and sceKernelAllocMemBlock change d0 and d1
// (0x00000003). All other registers stay as loaded.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "registers.h"
#include "test.h"
#include "threadmgr_extra.h"

#define PIPE_SIZE 8192
#define BIG 4096
#define SMALL 64
#define FPSCR_MARKER 0x01C00000

typedef int (*Call)(int, int, int, int);

// The d registers to load, the d registers read back after the call, and FPSCR before and after
static struct {
    unsigned long long markers[32];
    unsigned long long after[32];
    unsigned int fpscr[2];
} regs;
static unsigned char data[BIG];
static SceKernelThreadInfo thread_info = { .size = sizeof(thread_info) };
static SceKernelSemaInfo sema_info = { .size = sizeof(sema_info) };
static CallbackInfo callback_info = { .size = sizeof(callback_info) };

// The imports themselves, so nothing compiled here runs between the call and the syscall
#define TRY_SEND ((Call)(void *)sceKernelTrySendMsgPipe)
#define TRY_RECEIVE ((Call)(void *)sceKernelTryReceiveMsgPipe)
#define GET_THREAD_INFO ((Call)(void *)sceKernelGetThreadInfo)
#define GET_SEMA_INFO ((Call)(void *)sceKernelGetSemaInfo)
#define GET_PROCESS_TIME ((Call)(void *)sceKernelGetProcessTimeWide)
#define GET_SYSTEM_TIME ((Call)(void *)sceKernelGetSystemTimeWide)
#define POLL ((Call)(void *)sceKernelPollSema)
#define GET_CALLBACK_INFO ((Call)(void *)sceKernelGetCallbackInfo)
#define ALLOC_BLOCK ((Call)(void *)sceKernelAllocMemBlock)
#define FREE_BLOCK ((Call)(void *)sceKernelFreeMemBlock)

// Loads d0 to d31 from regs.markers and FPSCR from regs.fpscr[0], calls fn(a0, a1, a2, a3, a4),
// then stores d0 to d31 to regs.after and FPSCR to regs.fpscr[1]. a4 goes on the stack.
static int call_with_all_d(Call fn, int a0, int a1, int a2, int a3, int a4) {
    register int r0 __asm__("r0") = a0;
    register int r1 __asm__("r1") = a1;
    register int r2 __asm__("r2") = a2;
    register int r3 __asm__("r3") = a3;
    __asm__ volatile(
        "ldr r12, [%[b], #512]\n\t"
        "vmsr fpscr, r12\n\t"
        "vldmia %[b], {d0-d15}\n\t"
        "add r12, %[b], #128\n\t"
        "vldmia r12, {d16-d31}\n\t"
        "sub sp, sp, #8\n\t"
        "str %[a4], [sp]\n\t"
        "blx %[fn]\n\t"
        "add sp, sp, #8\n\t"
        "add r12, %[b], #256\n\t"
        "vstmia r12, {d0-d15}\n\t"
        "add r12, %[b], #384\n\t"
        "vstmia r12, {d16-d31}\n\t"
        "vmrs r12, fpscr\n\t"
        "str r12, [%[b], #516]\n\t"
        : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
        : [b] "r"(&regs), [fn] "r"(fn), [a4] "r"(a4)
        : "r12", "lr", "memory", "cc", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9",
          "d10", "d11", "d12", "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23",
          "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31");
    return r0;
}

// show_result is 0 for a result that is a UID or a time
static int run(const char *name, Call fn, int a0, int a1, int a2, int a3, int a4, int show_result) {
    regs.fpscr[0] = FPSCR_MARKER;
    regs.fpscr[1] = 0;
    const int result = call_with_all_d(fn, a0, a1, a2, a3, a4);
    if (show_result)
        PRINTF(result, "%s: the result", name);
    unsigned int changed = 0;
    for (int d = 0; d < 32; d++) {
        if (regs.after[d] != regs.markers[d])
            changed |= 1U << d;
    }
    PRINTF(changed, "%s: the d registers that changed, one bit each", name);
    PRINTF(regs.fpscr[1] != regs.fpscr[0], "%s: FPSCR changed", name);
    return result;
}

static SceUID make_pipe(void) {
    return sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, PIPE_SIZE, NULL);
}

static int callback_entry(int notify_id, int notify_count, int notify_arg, void *common) {
    (void)notify_id;
    (void)notify_count;
    (void)notify_arg;
    (void)common;
    return 0;
}

int main(void) {
    for (int d = 0; d < 32; d++)
        regs.markers[d] = (unsigned long long)(0xC1000000U + (unsigned int)d) << 32 | (0xC2000000U + (unsigned int)d);

    SceUID pipe = make_pipe();
    run("message pipe try send of 4 KiB", TRY_SEND, pipe, (int)data, BIG, MSG_PIPE_MODE_FULL, 0, 1);
    sceKernelDeleteMsgPipe(pipe);

    pipe = make_pipe();
    sceKernelTrySendMsgPipe(pipe, data, BIG, MSG_PIPE_MODE_FULL, NULL);
    run("message pipe try receive of 4 KiB", TRY_RECEIVE, pipe, (int)data, BIG, MSG_PIPE_MODE_FULL, 0, 1);
    sceKernelDeleteMsgPipe(pipe);

    pipe = make_pipe();
    run("message pipe try send of 64 bytes", TRY_SEND, pipe, (int)data, SMALL, MSG_PIPE_MODE_FULL, 0, 1);
    sceKernelDeleteMsgPipe(pipe);

    const SceUID thread = sceKernelGetThreadId();
    run("sceKernelGetThreadInfo", GET_THREAD_INFO, thread, (int)&thread_info, 0, 0, 0, 1);

    SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    run("sceKernelGetSemaInfo", GET_SEMA_INFO, sema, (int)&sema_info, 0, 0, 0, 1);
    sceKernelDeleteSema(sema);

    run("sceKernelGetProcessTimeWide", GET_PROCESS_TIME, 0, 0, 0, 0, 0, 0);
    run("sceKernelGetSystemTimeWide", GET_SYSTEM_TIME, 0, 0, 0, 0, 0, 0);

    sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    run("sceKernelPollSema that succeeds", POLL, sema, 1, 0, 0, 0, 1);
    sceKernelDeleteSema(sema);

    const SceUID callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    run("sceKernelGetCallbackInfo", GET_CALLBACK_INFO, callback, (int)&callback_info, 0, 0, 0, 1);
    sceKernelDeleteCallback(callback);

    const SceUID block = run("sceKernelAllocMemBlock of 64 KiB", ALLOC_BLOCK, (int)"block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x10000, 0, 0, 0);
    if (block >= 0)
        run("sceKernelFreeMemBlock of it", FREE_BLOCK, block, 0, 0, 0, 0, 1);

    test_exit();
    return 0;
}
