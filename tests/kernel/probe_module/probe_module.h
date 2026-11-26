// What probe_module's start and stop functions record, through a pointer in their arguments. They
// also leave a rounding mode, a VFP exception setting and TPIDRURW of their own on their thread.
#pragma once

#include <psp2/types.h>

typedef struct ModuleProbe {
    SceUID thread;
    SceUID process;
    int priority;
    int init_priority;
    int stack_size;
    unsigned int attr;
    SceUIntPtr frame;
    SceUIntPtr argp;
    SceSize args;
    int runs;
    unsigned int fpscr;
    unsigned int vfp_setting;
    unsigned int tpidrurw;
    SceUID module;
} ModuleProbe;

typedef struct ModuleArgs {
    ModuleProbe *out;
} ModuleArgs;
