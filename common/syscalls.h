// The kernel calls behind some libkernel exports, called directly with the argument blocks libkernel
// builds for them. Each is declared under its own name with an asm label, since the vitasdk headers
// declare some of these symbols with other arguments.
#pragma once

#include <psp2/kernel/threadmgr.h>
#include <psp2/types.h>

// Waits: {the result pattern pointer, the timeout pointer}
typedef struct WaitEventFlagBlock {
    SceUInt32 *result_pattern;
    SceUInt32 *timeout;
} WaitEventFlagBlock;

// Waits on a simple event: {the user data pointer, the timeout pointer}
typedef struct WaitEventBlock {
    SceUInt64 *user_data;
    SceUInt32 *timeout;
} WaitEventBlock;

SceInt32 syscall_wait_event_flag(SceUID id, SceUInt32 pattern, SceUInt32 mode, const WaitEventFlagBlock *block) __asm__("_sceKernelWaitEventFlag");
SceInt32 syscall_wait_event_flag_cb(SceUID id, SceUInt32 pattern, SceUInt32 mode, const WaitEventFlagBlock *block) __asm__("_sceKernelWaitEventFlagCB");
SceInt32 syscall_wait_event(SceUID id, SceUInt32 pattern, SceUInt32 *result, const WaitEventBlock *block) __asm__("_sceKernelWaitEvent");
SceInt32 syscall_wait_event_cb(SceUID id, SceUInt32 pattern, SceUInt32 *result, const WaitEventBlock *block) __asm__("_sceKernelWaitEventCB");

// Lightweight mutex lock, the last argument says whether callbacks run during the wait
SceInt32 syscall_lock_lw_mutex(SceKernelLwMutexWork *work, SceInt32 count, SceUInt32 *timeout, SceInt32 callbacks) __asm__("_sceKernelLockLwMutex");

// Module calls: {flags, the option pointer, the status pointer}
typedef struct ModuleCallBlock {
    SceUInt32 flags;
    const void *opt;
    int *status;
} ModuleCallBlock;

SceUID syscall_load_start_module(const char *path, SceSize args, const void *argp, const ModuleCallBlock *block) __asm__("_sceKernelLoadStartModule");
SceInt32 syscall_start_module(SceUID module, SceSize args, const void *argp, const ModuleCallBlock *block) __asm__("_sceKernelStartModule");
SceInt32 syscall_stop_module(SceUID module, SceSize args, const void *argp, const ModuleCallBlock *block) __asm__("_sceKernelStopModule");
SceInt32 syscall_stop_unload_module(SceUID module, SceSize args, const void *argp, const ModuleCallBlock *block) __asm__("_sceKernelStopUnloadModule");

// File calls. The option blocks libkernel passes are never filled in.
typedef struct IoOptBlock {
    SceUInt32 words[5];
} IoOptBlock;

// A 64 bit file offset as two words
typedef struct IoOffsetBlock {
    SceUInt32 low;
    SceUInt32 high;
} IoOffsetBlock;

// sceIoDevctl and sceIoIoctl: {the input length, the output buffer, the output length}
typedef struct IoControlBlock {
    SceSize in_length;
    void *out;
    SceSize out_length;
} IoControlBlock;

SceInt32 syscall_io_remove(const char *path, const IoOptBlock *opt) __asm__("_sceIoRemove");
SceInt32 syscall_io_rename(const char *old_path, const char *new_path, const IoOptBlock *opt) __asm__("_sceIoRename");
SceInt32 syscall_io_mkdir(const char *path, SceMode mode, const IoOptBlock *opt) __asm__("_sceIoMkdir");
SceInt32 syscall_io_rmdir(const char *path, const IoOptBlock *opt) __asm__("_sceIoRmdir");
SceInt32 syscall_io_sync(const char *device, SceUInt32 flag, const IoOptBlock *opt) __asm__("_sceIoSync");
SceSSize syscall_io_pread(SceUID fd, void *buf, SceSize size, const IoOffsetBlock *offset) __asm__("_sceIoPread");
SceSSize syscall_io_pwrite(SceUID fd, const void *buf, SceSize size, const IoOffsetBlock *offset) __asm__("_sceIoPwrite");
SceInt32 syscall_io_devctl(const char *device, SceInt32 cmd, const void *in, const IoControlBlock *block) __asm__("_sceIoDevctl");
SceInt32 syscall_io_ioctl(SceUID fd, SceInt32 cmd, const void *in, const IoControlBlock *block) __asm__("_sceIoIoctl");
