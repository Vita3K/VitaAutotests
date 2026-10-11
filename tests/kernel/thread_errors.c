// Every thread call that takes a thread UID with one that isn't a thread it can use, and other
// arguments it should reject. UID 0 means the running thread for these, so it isn't one of them.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

int main(void) {
    const SceUID deleted = sceKernelCreateThread("deleted", entry, 0x10000100, 0x4000, 0, 0, NULL);
    sceKernelDeleteThread(deleted);
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    const SceUID ids[] = { 0x12345, deleted, sema };
    const char *const names[] = { "an unknown UID", "a deleted thread", "a semaphore" };

    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        SceUInt timeout = 1000;
        int status = 0xDEAD;
        SceKernelThreadInfo info = { .size = sizeof(info) };
        PRINTF(sceKernelStartThread(ids[i], 0, NULL), "start %s", names[i]);
        PRINTF(sceKernelWaitThreadEnd(ids[i], &status, &timeout), "wait for the end of %s", names[i]);
        timeout = 1000;
        PRINTF(sceKernelWaitThreadEndCB(ids[i], &status, &timeout), "wait with callbacks for the end of %s", names[i]);
        PRINTF(sceKernelGetThreadExitStatus(ids[i], &status), "the exit status of %s", names[i]);
        PRINTF(sceKernelGetThreadInfo(ids[i], &info), "info of %s", names[i]);
        PRINTF(sceKernelChangeThreadPriority(ids[i], 0x10000100), "change the priority of %s", names[i]);
        PRINTF(sceKernelGetThreadStackFreeSize(ids[i]), "the stack free size of %s", names[i]);
        PRINTF(sceKernelGetThreadCpuAffinityMask(ids[i]), "the affinity of %s", names[i]);
        PRINTF(sceKernelChangeThreadCpuAffinityMask(ids[i], CPU_MASK_USER_0), "change the affinity of %s", names[i]);
        PRINTF(sceKernelDeleteThread(ids[i]), "delete %s", names[i]);
    }
    PRINT("the semaphore survives the thread calls", sceKernelPollSema(sema, 1));

    // Creation
    PRINT("create with an empty name", test_created(sceKernelCreateThread("", entry, 0x10000100, 0x4000, 0, 0, NULL)));
    PRINT("create with attribute 0x80000000", test_created(sceKernelCreateThread("t", entry, 0x10000100, 0x4000, 0x80000000, 0, NULL)));
    PRINT("create with a stack of 4 KiB", test_created(sceKernelCreateThread("t", entry, 0x10000100, 0x1000, 0, 0, NULL)));
    PRINT("create with a stack of 4 KiB minus 1", test_created(sceKernelCreateThread("t", entry, 0x10000100, 0xFFF, 0, 0, NULL)));
    PRINT("create with a stack of 1 KiB", test_created(sceKernelCreateThread("t", entry, 0x10000100, 0x400, 0, 0, NULL)));
    PRINT("create with a 4 KiB plus 1 stack", test_created(sceKernelCreateThread("t", entry, 0x10000100, 0x1001, 0, 0, NULL)));
    PRINT("create with every user core", test_created(sceKernelCreateThread("t", entry, 0x10000100, 0x4000, 0, 0x70000, NULL)));
    PRINT("create with a core games can't use", test_created(sceKernelCreateThread("t", entry, 0x10000100, 0x4000, 0, 0x80000, NULL)));
    PRINT("create with affinity bit 0", test_created(sceKernelCreateThread("t", entry, 0x10000100, 0x4000, 0, 0x1, NULL)));

    // A dormant thread
    const SceUID dormant = sceKernelCreateThread("dormant", entry, 0x10000100, 0x4000, 0, 0, NULL);
    PRINT("change the priority of a dormant thread", sceKernelChangeThreadPriority(dormant, 0x10000101));
    PRINT("change it to priority 0", sceKernelChangeThreadPriority(dormant, 0));
    PRINT("change its affinity", sceKernelChangeThreadCpuAffinityMask(dormant, CPU_MASK_USER_1));
    PRINT("its affinity after it", sceKernelGetThreadCpuAffinityMask(dormant));
    PRINT("its stack free size is positive", sceKernelGetThreadStackFreeSize(dormant) > 0);
    sceKernelDeleteThread(dormant);

    // The running thread's attributes
    PRINT("change the current thread attribute with nothing", sceKernelChangeCurrentThreadAttr(0, 0));
    PRINT("set attribute 0x80000000", sceKernelChangeCurrentThreadAttr(0, 0x80000000));
    PRINT("clear attribute 0x80000000", sceKernelChangeCurrentThreadAttr(0x80000000, 0));
    PRINT("set the VFP attribute", sceKernelChangeCurrentThreadAttr(0, 0x10000));

    // Info
    SceKernelThreadInfo info = { .size = 0 };
    PRINT("info with a size of 0", sceKernelGetThreadInfo(0, &info));
    // With room behind the struct, in case the call writes as much as the size says
    static union {
        SceKernelThreadInfo info;
        unsigned char bytes[sizeof(SceKernelThreadInfo) + 64];
    } big;
    sceClibMemset(&big, 0xEE, sizeof(big));
    big.info.size = sizeof(SceKernelThreadInfo) + 4;
    PRINT("info with a size past the struct", sceKernelGetThreadInfo(0, &big.info));
    int untouched = 1;
    for (unsigned int i = sizeof(SceKernelThreadInfo); i < sizeof(big.bytes); i++)
        untouched &= big.bytes[i] == 0xEE;
    PRINT("it leaves the bytes past the struct alone", untouched);

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("info with a NULL pointer", sceKernelGetThreadInfo(0, NULL));
    const SceUID unstarted = sceKernelCreateThread("unstarted", entry, 0x10000100, 0x4000, 0, 0, NULL);
    PRINT("start with an argument size but no argument", sceKernelStartThread(unstarted, 4, NULL));
    SceUInt timeout = TEST_WAIT_US;
    PRINT("wait for its end", sceKernelWaitThreadEnd(unstarted, NULL, &timeout));
    PRINT("the exit status into a NULL pointer", sceKernelGetThreadExitStatus(sceKernelCreateThread("t", entry, 0x10000100, 0x4000, 0, 0, NULL), NULL));

    test_exit();
    return 0;
}
