// The results of sceKernelSuspendThreadForVM and sceKernelResumeThreadForVM for UIDs that can't be
// suspended: UID 0, two unknown UIDs, a deleted thread with the VM attribute, a deleted thread
// without it, a semaphore, a live thread without the VM attribute, and the VM thread itself. Each
// target is tried with a suspend and then a resume, from two callers: the main thread, which has no
// VM attribute, and the VM thread V. The main thread runs on core 1, and V and the live thread
// without the VM attribute run on core 0. The cases:
// - the main thread makes the calls on each target in the order above, with V's own UID last, so
//   the suspend count and the resume of V are printed too
// - V makes the same calls, where its own UID is the one target that is V itself, and it comes last
//   since a wrong implementation could suspend V itself
// - a deleted thread without the VM attribute is tried next to a deleted thread with it, to show
//   whether the UID is looked up before the attribute is checked
// - a live thread without the VM attribute is tried, to show how it is refused
// Each caller's results are printed in the order of the calls. V's results are read after it ends,
// and a resume of V is made while it runs its calls, so a wrong implementation can't hold the test.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define T_PRIORITY 0xA0
#define TARGETS 8

static const char *const target_names[TARGETS] = {
    "UID 0",
    "an unknown UID",
    "another unknown UID",
    "a deleted thread with the VM attribute",
    "a deleted thread without it",
    "a semaphore",
    "a live thread without the VM attribute",
    "the VM thread",
};

static SceUID sema, gate, nv_gate, nv, vm;
static SceUID targets[TARGETS];
static int vm_suspend[TARGETS], vm_resume[TARGETS];
static volatile int vm_done;

// Waits until the main thread opens the gate, then suspends and resumes each target in order
static int vm_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    for (int i = 0; i < TARGETS; i++) {
        vm_suspend[i] = sceKernelSuspendThreadForVM(targets[i]);
        vm_resume[i] = sceKernelResumeThreadForVM(targets[i]);
    }
    vm_done = 1;
    return 0;
}

// Waits on its own semaphore, so it stays alive and without the VM attribute
static int plain_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(nv_gate, 1, &timeout);
    return 0;
}

// Suspends and resumes one target from the main thread, and prints both results
static void main_calls(int i) {
    const int suspended = sceKernelSuspendThreadForVM(targets[i]);
    const int resumed = sceKernelResumeThreadForVM(targets[i]);
    PRINTF(suspended, "main: suspend %s", target_names[i]);
    PRINTF(resumed, "main: resume %s", target_names[i]);
}

// Resumes V until its calls are done. A correct implementation refuses these resumes, and a
// wrong one that suspends V itself can't hold the test.
static void wait_for_vm(void) {
    for (int n = 0; n < 5000 && !vm_done; n++) {
        sceKernelResumeThreadForVM(vm);
        sceKernelDelayThread(1000);
    }
}

int main(void) {
    const int original_mask = sceKernelGetThreadCpuAffinityMask(0);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    nv_gate = sceKernelCreateSema("nv gate", 0, 0, 1, NULL);
    nv = sceKernelCreateThread("plain", plain_entry, T_PRIORITY, 0x4000, 0,
                               SCE_KERNEL_CPU_MASK_USER_0, NULL);
    vm = sceKernelCreateThread("vm", vm_entry, T_PRIORITY, 0x4000, THREAD_ATTR_VM,
                               SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(nv, 0, NULL);
    sceKernelStartThread(vm, 0, NULL);

    // Two threads created and deleted before they start, so their UIDs are unknown. Nothing is
    // created after them, so their UIDs can't come back for another object.
    const SceUID dead_vm = sceKernelCreateThread("dead", plain_entry, T_PRIORITY, 0x4000,
                                                 THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID dead_plain = sceKernelCreateThread("dead", plain_entry, T_PRIORITY, 0x4000, 0,
                                                    SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelDeleteThread(dead_vm);
    sceKernelDeleteThread(dead_plain);

    targets[0] = 0;
    targets[1] = 0x12345;
    targets[2] = 0x12347;
    targets[3] = dead_vm;
    targets[4] = dead_plain;
    targets[5] = sema;
    targets[6] = nv;
    targets[7] = vm;

    TEST_WAIT_FOR(test_in_wait(vm, WAIT_TYPE_SEMA, gate));
    TEST_WAIT_FOR(test_in_wait(nv, WAIT_TYPE_SEMA, nv_gate));
    for (int i = 0; i < TARGETS; i++)
        main_calls(i);

    sceKernelSignalSema(gate, 1);
    wait_for_vm();
    test_join_thread(vm);
    sceKernelSignalSema(nv_gate, 1);
    test_join_thread(nv);

    for (int i = 0; i < TARGETS; i++) {
        PRINTF(vm_suspend[i], "the VM thread: suspend %s", target_names[i]);
        PRINTF(vm_resume[i], "the VM thread: resume %s", target_names[i]);
    }

    sceKernelDeleteSema(sema);
    sceKernelDeleteSema(gate);
    sceKernelDeleteSema(nv_gate);
    sceKernelChangeThreadCpuAffinityMask(0, original_mask);
    test_exit();
    return 0;
}
