// Shared by the tests of display waits with callbacks. The main thread runs at 0x48 and the waiter
// W at 0xB0, both pinned to core 0, so W only runs while the main thread is blocked. A wait under
// test starts right after a vblank, since W first waits for one without callbacks, so the next
// vblank is a whole frame, 16.7 ms, away. The tests count frames with sceDisplayGetVcount and never
// print times.
#pragma once

#include <psp2/display.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x48
#define W_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define THREAD_STATUS_WAITING 0x8
#define MAX_STEPS 8

// What the waiter did, in order, for the tests that print it
static volatile int steps[MAX_STEPS] __attribute__((unused));
static volatile int step_count __attribute__((unused));

// Returns the vblanks since the count v0. sceDisplayGetVcount keeps only 16 bits.
static inline int vblanks_since(int v0) {
    return (sceDisplayGetVcount() - v0) & 0xFFFF;
}

// Waits for a vblank without callbacks and returns the count right after it
static inline int align_to_vblank(void) {
    sceDisplayWaitVblankStart();
    return sceDisplayGetVcount();
}

// Pins the main thread to core 0 at MAIN_PRIORITY
static inline void pin_main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);
}

// Starts the waiter W on core 0 at W_PRIORITY
static inline SceUID start_w(SceKernelThreadEntry entry) {
    const SceUID thread = sceKernelCreateThread("w", entry, W_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

static inline SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

static inline SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

// Returns whether thread is in a wait with callbacks
static inline int in_cb_wait(SceUID thread) {
    return (wait_type_of(thread) & WAIT_TYPE_CB) != 0;
}

static inline void reset_steps(void) {
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = 0;
}

static inline void step(int code) {
    if (step_count < MAX_STEPS)
        steps[step_count] = code;
    step_count++;
}

static inline void print_steps(const char *name) {
    PRINTF(step_count, "%s: steps", name);
    for (int i = 0; i < step_count && i < MAX_STEPS; i++)
        PRINTF(steps[i], "%s: step %d", name, i);
}
