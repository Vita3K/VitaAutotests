// sceNgsSystemUpdate, one step at a time: a system with no racks, with an idle master buss, with
// simple voices patched into it, and with the master and then a voice playing, without a buffer.
// A test of its own, since an update can hang, and each step prints before the next one runs.
#include <psp2/kernel/sysmem.h>
#include <psp2/ngs.h>
#include <psp2/sysmodule.h>

#include "test.h"

#define MEMORY_SIZE 0x100000

static char *memory;
static SceSize used;

static void *take(SceSize size) {
    void *p = memory + used;
    used += (size + 255) & ~255u;
    return p;
}

static void print_state(const char *what, SceNgsHVoice voice) {
    SceNgsVoiceInfo info;
    sceClibMemset(&info, 0x55, sizeof(info));
    const int ret = sceNgsVoiceGetInfo(voice, &info);
    PRINTF(ret == 0 ? (int)info.voice_state : ret, "%s: its state", what);
    PRINTF(ret == 0 ? (int)info.update_passed : ret, "%s: its updates", what);
}

int main(void) {
    PRINT("load the ngs module", sceSysmoduleLoadModule(SCE_SYSMODULE_NGS));
    const SceUID block = sceKernelAllocMemBlock("ngs_update", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, MEMORY_SIZE, NULL);
    sceKernelGetMemBlockBase(block, (void **)&memory);

    const SceNgsSystemInitParams params = { .max_racks = 4, .max_voices = 8, .granularity = 512, .sample_rate = 48000, .reserved = 0 };
    SceSize system_size = 0;
    sceNgsSystemGetRequiredMemorySize(&params, &system_size);
    SceNgsHSynSystem system = 0;
    PRINT("init a system", sceNgsSystemInit(take(system_size), system_size, &params, &system));
    PRINT("update it with no racks", sceNgsSystemUpdate(system));
    PRINT("update it again", sceNgsSystemUpdate(system));

    const SceNgsRackDescription master_desc = { sceNgsVoiceDefGetMasterBuss(), 1, 2, 4, 0, NULL };
    SceSize size = 0;
    sceNgsRackGetRequiredMemorySize(system, &master_desc, &size);
    SceNgsBufferInfo master_buffer = { take(size), size };
    SceNgsHRack master_rack = 0;
    PRINT("init a master rack", sceNgsRackInit(system, &master_buffer, &master_desc, &master_rack));
    SceNgsHVoice master = 0;
    sceNgsRackGetVoiceHandle(master_rack, 0, &master);
    PRINT("update with an idle master", sceNgsSystemUpdate(system));
    print_state("the idle master", master);

    const SceNgsRackDescription voice_desc = { sceNgsVoiceDefGetSimpleVoice(), 2, 2, 0, 1, NULL };
    sceNgsRackGetRequiredMemorySize(system, &voice_desc, &size);
    SceNgsBufferInfo voice_buffer = { take(size), size };
    SceNgsHRack voice_rack = 0;
    PRINT("init a rack of two simple voices", sceNgsRackInit(system, &voice_buffer, &voice_desc, &voice_rack));
    SceNgsHVoice voice = 0;
    sceNgsRackGetVoiceHandle(voice_rack, 0, &voice);
    SceNgsPatchSetupInfo setup = { voice, 0, 0, master, 0 };
    SceNgsHPatch patch = 0;
    PRINT("patch voice 0 into the master", sceNgsPatchCreateRouting(&setup, &patch));
    PRINT("update with idle voices", sceNgsSystemUpdate(system));
    print_state("the idle voice", voice);

    PRINT("play the master", sceNgsVoicePlay(master));
    print_state("the master after play", master);
    PRINT("update with the master playing", sceNgsSystemUpdate(system));
    print_state("the master after the update", master);
    PRINT("update again", sceNgsSystemUpdate(system));
    print_state("the master after two updates", master);

    PRINT("play voice 0 without a buffer", sceNgsVoicePlay(voice));
    print_state("voice 0 after play", voice);
    PRINT("update with it playing", sceNgsSystemUpdate(system));
    print_state("voice 0 after the update", voice);
    PRINT("update again", sceNgsSystemUpdate(system));
    print_state("voice 0 after two updates", voice);
    PRINT("key off voice 0", sceNgsVoiceKeyOff(voice));
    PRINT("update after it", sceNgsSystemUpdate(system));
    print_state("voice 0 after key off and an update", voice);

    PRINT("release the voice rack", sceNgsRackRelease(voice_rack, NULL));
    PRINT("release the master rack", sceNgsRackRelease(master_rack, NULL));
    PRINT("update without racks", sceNgsSystemUpdate(system));
    PRINT("release the system", sceNgsSystemRelease(system));
    PRINT("update the released system", sceNgsSystemUpdate(system));
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NGS);

    sceKernelFreeMemBlock(block);
    test_exit();
    return 0;
}
