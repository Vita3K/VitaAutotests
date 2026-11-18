// Shared by thread_vm_bkpt and thread_vm_undefined. A THREAD_ATTR_VM worker runs an instruction that
// faults, and the main thread then asks for the exception record with sceKernelWaitException.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

typedef struct {
    SceSize size;
    SceUID thread;
    SceUInt32 type;
    SceUInt32 unknown0;
    SceUInt32 address;
    SceUInt32 unknown1;
} VmExceptionInfo;

int sceKernelWaitException(int type, VmExceptionInfo *info, SceUInt *timeout);

__attribute__((naked, target("arm"), unused)) static int vm_worker_bkpt(SceSize, void *) {
    __asm__ volatile(".word 0xe1200070\n\tbx lr");
}

__attribute__((naked, target("arm"), unused)) static int vm_worker_undefined(SceSize, void *) {
    __asm__ volatile(".word 0xe7f000f0\n\tbx lr");
}

static void vm_fault_run(SceKernelThreadEntry entry) {
    const SceUID worker = sceKernelCreateThread("worker", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, THREAD_ATTR_VM, 0, NULL);
    PRINT("create the worker", worker > 0);
    PRINT("start the worker", sceKernelStartThread(worker, 0, NULL));
    VmExceptionInfo info = { .size = sizeof(info) };
    SceUInt timeout = TEST_WAIT_US;
    PRINT("wait for the exception", sceKernelWaitException(0x200, &info, &timeout));
    PRINT("the record is of the worker", info.thread == worker);
    PRINT("its type", info.type);
    SceKernelThreadInfo thread = { .size = sizeof(thread) };
    PRINT("get the worker's info", sceKernelGetThreadInfo(worker, &thread));
    PRINT("its status", thread.status);
}
