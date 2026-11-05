// Which FPSCR a thread has after sceKernelSetThreadContextForVM wrote one while the thread waited
// in a syscall. The thread sets its rounding mode to round towards plus infinity and waits on a
// semaphore. The test suspends it, reads the VFP registers, writes them back with the rounding
// mode changed to round towards minus infinity, and reads them again at once. Then it resumes the
// thread, signals the semaphore, and prints the rounding mode the thread has after the wait. A
// second run does the same without the write, to show what the thread has after the wait on its
// own. The control bits of the FPSCR are printed before and after the write.
//
// On a PS Vita TV on firmware 3.65 the read shows the control bits 0x03000000 and the read after
// the write shows them again. Both waits return 0. Without the write the thread has rounding mode 1
// after the wait, round towards plus infinity, as it set it. With the write it has rounding mode 2,
// round towards minus infinity, the one that was written.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "registers.h"
#include "test.h"
#include "threadmgr_extra.h"

int sceKernelSetThreadContextForVM(SceUID thid, const ThreadCpuRegisters *cpu, const ThreadVfpRegisters *vfp);

static SceUID sema, target;
static volatile unsigned int fpscr_after, wait_result;

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    set_rmode(RMODE_PLUS);
    SceUInt timeout = TEST_WAIT_US;
    wait_result = (unsigned int)sceKernelWaitSema(sema, 1, &timeout);
    fpscr_after = get_fpscr();
    return 0;
}

static void run(const char *name, int write) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    fpscr_after = 0xFFFFFFFF;
    wait_result = 0xFFFFFFFF;
    sceKernelStartThread(target, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(target, WAIT_TYPE_SEMA, sema));
    PRINTF(sceKernelSuspendThreadForVM(target), "%s: suspend it", name);

    ThreadCpuRegisters cpu = { .size = sizeof(cpu) };
    ThreadVfpRegisters vfp = { .size = sizeof(vfp) };
    PRINTF(sceKernelGetThreadContextForVM(target, &cpu, &vfp), "%s: read it", name);
    PRINTF(vfp.fpscr & FPSCR_CONTROL, "%s: the FPSCR control bits", name);
    if (write) {
        vfp.fpscr = (vfp.fpscr & ~(3u << FPSCR_RMODE_SHIFT)) | ((unsigned int)RMODE_MINUS << FPSCR_RMODE_SHIFT);
        PRINTF(sceKernelSetThreadContextForVM(target, NULL, &vfp), "%s: write the FPSCR with round towards minus infinity", name);
        ThreadVfpRegisters back = { .size = sizeof(back) };
        PRINTF(sceKernelGetThreadContextForVM(target, &cpu, &back), "%s: read it again", name);
        PRINTF(back.fpscr & FPSCR_CONTROL, "%s: the FPSCR control bits again", name);
    }

    PRINTF(sceKernelResumeThreadForVM(target), "%s: resume it", name);
    PRINTF(sceKernelSignalSema(sema, 1), "%s: signal it", name);
    test_join_thread(target);
    PRINTF(wait_result, "%s: the wait result", name);
    PRINTF((fpscr_after >> FPSCR_RMODE_SHIFT) & 3, "%s: the thread's rounding mode after the wait", name);
    sceKernelDeleteSema(sema);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    run("no write", 0);
    run("write", 1);
    test_exit();
    return 0;
}
