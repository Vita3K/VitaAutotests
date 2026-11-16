// libkernel imports from extra_imports.S: functions the vitasdk stubs lack and a variable whose
// stub name newlib also defines
#pragma once

#include <psp2/kernel/threadmgr.h>
#include <psp2/types.h>

// libkernel's own sceKernelGetThreadTLSAddr. Thread 0 or the caller's UID gives the caller's slot,
// another thread goes to the thread manager call.
void *SceLibKernel_35D20E49(SceUID thread, int key);

// The ARM coded sceKernelLockLwMutexCB
int SceLibKernel_6F9C4CC1(SceKernelLwMutexWork *work, int count, SceUInt32 *timeout);

// libkernel's __stack_chk_guard, under another name since newlib has its own
extern SceUInt32 libkernel_stack_chk_guard;

// The second copy of the stack canary that libkernel keeps
extern SceUInt32 SceKernelStackChkGuard;
