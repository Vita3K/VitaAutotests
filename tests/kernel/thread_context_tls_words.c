// The last three words of the CPU block of sceKernelGetThreadContextForVM and
// sceKernelSetThreadContextForVM. The test fills them with markers, reads a suspended thread that
// waits on a semaphore, and prints whether each word kept its marker or has the value of the
// thread's TLS slot 2 or 3, which the thread stored before it waited. Then it writes new values
// into words 20 and 21, reads again, resumes the thread and signals the semaphore. If the thread
// gets past its wait, the test prints whether it sees the new values in its slots 2 and 3, or still
// has its old values. If it doesn't, the test prints its status, its wait type and the semaphore's
// count. The thread puts its old slots back before it ends.
//
// On a PS Vita TV on firmware 3.65 a read leaves word 19 at its marker and gives the values of the
// thread's TLS slots 2 and 3 in words 20 and 21. A write of new words 20 and 21 succeeds and the
// next read shows them, with word 19 still at its marker. The resume returns 0, but after the
// signal the thread doesn't get past its wait. It shows SUSPENDED 0x100 with wait type 0, and the
// semaphore has count 0.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int sceKernelSetThreadContextForVM(SceUID thid, const ThreadCpuRegisters *cpu, const ThreadVfpRegisters *vfp);

#define MARKER_19 0xA5A5A519
#define MARKER_20 0xA5A5A520
#define MARKER_21 0xA5A5A521
#define NEW_20 0x12345620
#define NEW_21 0x12345621

static SceUID sema;
static volatile SceUInt32 slot2_before, slot3_before, slot2_after, slot3_after;
static volatile int stored, ended;

static int sema_count(void) {
    SceKernelSemaInfo info = { .size = sizeof(info) };
    sceKernelGetSemaInfo(sema, &info);
    return info.currentCount;
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    volatile SceUInt32 *slot2 = sceKernelGetTLSAddr(2);
    volatile SceUInt32 *slot3 = sceKernelGetTLSAddr(3);
    slot2_before = *slot2;
    slot3_before = *slot3;
    stored = 1;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    // The context write can change the registers that held the pointers, so they are taken again
    slot2 = sceKernelGetTLSAddr(2);
    slot3 = sceKernelGetTLSAddr(3);
    slot2_after = *slot2;
    slot3_after = *slot3;
    *slot2 = slot2_before;
    *slot3 = slot3_before;
    ended = 1;
    return 0;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(target, 0, NULL);
    TEST_WAIT_FOR(stored && test_in_wait(target, WAIT_TYPE_SEMA, sema));

    ThreadCpuRegisters cpu = { .size = sizeof(cpu) };
    cpu.teehbr = MARKER_19;
    cpu.sb = MARKER_20;
    cpu.st = MARKER_21;
    PRINT("suspend it", sceKernelSuspendThreadForVM(target));
    PRINT("read it", sceKernelGetThreadContextForVM(target, &cpu, NULL));
    PRINT("word 19 keeps its marker", cpu.teehbr == MARKER_19);
    PRINT("word 20 is the thread's TLS slot 2", cpu.sb == slot2_before);
    PRINT("word 21 is the thread's TLS slot 3", cpu.st == slot3_before);

    cpu.teehbr = MARKER_19;
    cpu.sb = NEW_20;
    cpu.st = NEW_21;
    PRINT("write new words 20 and 21", sceKernelSetThreadContextForVM(target, &cpu, NULL));
    cpu.teehbr = MARKER_19;
    cpu.sb = MARKER_20;
    cpu.st = MARKER_21;
    PRINT("read it again", sceKernelGetThreadContextForVM(target, &cpu, NULL));
    PRINT("the read shows the new word 20", cpu.sb == NEW_20);
    PRINT("the read shows the new word 21", cpu.st == NEW_21);
    PRINT("word 19 keeps its marker again", cpu.teehbr == MARKER_19);

    PRINT("resume it", sceKernelResumeThreadForVM(target));
    sceKernelSignalSema(sema, 1);
    TEST_WAIT_FOR(ended);
    PRINT("the thread got past its wait", ended);
    if (ended) {
        PRINT("after the wait, its slot 2 is the new word 20", slot2_after == NEW_20);
        PRINT("after the wait, its slot 3 is the new word 21", slot3_after == NEW_21);
        PRINT("after the wait, its slot 2 is the old one", slot2_after == slot2_before);
        PRINT("after the wait, its slot 3 is the old one", slot3_after == slot3_before);
    } else {
        SceKernelThreadInfo info = { .size = sizeof(info) };
        sceKernelGetThreadInfo(target, &info);
        PRINT("the thread's status", info.status);
        PRINT("the thread's wait type", info.waitType);
        PRINT("the semaphore's count", sema_count());
    }

    SceUInt timeout = 5000000;
    sceKernelWaitThreadEnd(target, NULL, &timeout);
    sceKernelDeleteThread(target);
    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
