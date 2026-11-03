// NGS: the memory a system and its racks need for each voice definition and set of params, which
// params they check, a master buss with simple voices patched into it, the voices' info, modules and
// states, patch routes, and releasing in each order. Nothing is heard, since no voice has a buffer
// and nothing outputs the master buss. Updating the system is in ngs_update.
#include <psp2/kernel/sysmem.h>
#include <psp2/ngs.h>
#include <psp2/sysmodule.h>

#include "test.h"

#define MEMORY_SIZE 0x400000

static char *memory;
static SceSize used;

// Takes size bytes of the memory block, aligned to 256
static void *take(SceSize size) {
    void *p = memory + used;
    used += (size + 255) & ~255u;
    return p;
}

static int float_bits(float f) {
    union {
        float f;
        int i;
    } u = { .f = f };
    return u.i;
}

static const SceNgsSystemInitParams base_params = { .max_racks = 4, .max_voices = 8, .granularity = 512, .sample_rate = 48000, .reserved = 0 };

// Prints the size a system needs with these params, or the error
static void print_system_size(const char *what, SceNgsSystemInitParams params) {
    SceSize size = 0x55555555;
    const int ret = sceNgsSystemGetRequiredMemorySize(&params, &size);
    PRINTF(ret == 0 ? (int)size : ret, "system memory with %s", what);
}

static void print_voice_info(const char *what, SceNgsHVoice voice) {
    SceNgsVoiceInfo info;
    sceClibMemset(&info, 0x55, sizeof(info));
    const int ret = sceNgsVoiceGetInfo(voice, &info);
    PRINTF(ret, "%s: get its info", what);
    if (ret == 0) {
        PRINTF(info.voice_state, "%s: its state", what);
        PRINTF(info.num_modules, "%s: its modules", what);
        PRINTF(info.num_inputs, "%s: its inputs", what);
        PRINTF(info.num_outputs, "%s: its outputs", what);
        PRINTF(info.num_patches_per_output, "%s: its patches per output", what);
        PRINTF(info.update_passed, "%s: its updates", what);
    }
}

int main(void) {
    PRINT("load the ngs module", sceSysmoduleLoadModule(SCE_SYSMODULE_NGS));
    const SceUID block = sceKernelAllocMemBlock("ngs", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, MEMORY_SIZE, NULL);
    sceKernelGetMemBlockBase(block, (void **)&memory);

    // System memory and the params it checks
    SceNgsSystemInitParams params = base_params;
    print_system_size("4 racks, 8 voices and a granularity of 512", params);
    static const int racks[] = { 0, 1, 64, 65 };
    for (unsigned int i = 0; i < sizeof(racks) / sizeof(racks[0]); i++) {
        params = base_params;
        params.max_racks = racks[i];
        char what[48];
        sceClibSnprintf(what, sizeof(what), "%d racks", racks[i]);
        print_system_size(what, params);
    }
    static const int voices[] = { 0, 1, 256, 257 };
    for (unsigned int i = 0; i < sizeof(voices) / sizeof(voices[0]); i++) {
        params = base_params;
        params.max_voices = voices[i];
        char what[48];
        sceClibSnprintf(what, sizeof(what), "%d voices", voices[i]);
        print_system_size(what, params);
    }
    static const int granularities[] = { 0, 32, 64, 100, 128, 256, 448, 576, 1024 };
    for (unsigned int i = 0; i < sizeof(granularities) / sizeof(granularities[0]); i++) {
        params = base_params;
        params.granularity = granularities[i];
        char what[48];
        sceClibSnprintf(what, sizeof(what), "a granularity of %d", granularities[i]);
        print_system_size(what, params);
    }
    static const int rates[] = { 0, 44100, 96000 };
    for (unsigned int i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        params = base_params;
        params.sample_rate = rates[i];
        char what[48];
        sceClibSnprintf(what, sizeof(what), "a rate of %d", rates[i]);
        print_system_size(what, params);
    }
    params = base_params;
    params.reserved = -1;
    print_system_size("reserved -1", params);
    params.reserved = 7;
    print_system_size("reserved 7", params);

    // Initializing a system
    SceSize system_size = 0;
    sceNgsSystemGetRequiredMemorySize(&base_params, &system_size);
    SceNgsHSynSystem system = 0;
    void *system_memory = take(system_size + 256);
    PRINT("init a system in a byte less than it needs", sceNgsSystemInit(system_memory, system_size - 1, &base_params, &system));
    PRINT("init a system 4 bytes past alignment", sceNgsSystemInit((char *)system_memory + 4, system_size, &base_params, &system));
    params = base_params;
    params.granularity = 100;
    PRINT("init a system with a granularity of 100", sceNgsSystemInit(system_memory, system_size, &params, &system));
    PRINT("init a system", sceNgsSystemInit(system_memory, system_size, &base_params, &system));
    PRINT("its handle is set", system != 0);
    PRINT("set its flags", sceNgsSystemSetFlags(system, SCE_NGS_SYSTEM_FLAG_CHECK_PARAMS));
    PRINT("lock it", sceNgsSystemLock(system));
    PRINT("unlock it", sceNgsSystemUnlock(system));
    PRINT("unlock it again", sceNgsSystemUnlock(system));

    // Rack memory for each voice definition, mono and stereo
    static const struct {
        const char *name;
        const SceNgsVoiceDefinition *(*get)(void);
    } defs[] = {
        { "master buss", sceNgsVoiceDefGetMasterBuss },
        { "simple voice", sceNgsVoiceDefGetSimpleVoice },
        { "atrac9 voice", sceNgsVoiceDefGetAtrac9Voice },
        { "simple atrac9 voice", sceNgsVoiceDefGetSimpleAtrac9Voice },
        { "scream voice", sceNgsVoiceDefGetScreamVoice },
        { "scream atrac9 voice", sceNgsVoiceDefGetScreamAtrac9Voice },
        { "sas emu voice", sceNgsVoiceDefGetSasEmuVoice },
        { "template 1", sceNgsVoiceDefGetTemplate1 },
        { "mixer buss", sceNgsVoiceDefGetMixerBuss },
        { "reverb buss", sceNgsVoiceDefGetReverbBuss },
        { "delay buss", sceNgsVoiceDefGetDelayBuss },
        { "distortion buss", sceNgsVoiceDefGetDistortionBuss },
        { "eq buss", sceNgsVoiceDefGetEqBuss },
        { "envelope buss", sceNgsVoiceDefGetEnvelopeBuss },
        { "compressor buss", sceNgsVoiceDefGetCompressorBuss },
        { "compressor side chain buss", sceNgsVoiceDefGetCompressorSideChainBuss },
        { "pauser buss", sceNgsVoiceDefGetPauserBuss },
        { "pitch shift buss", sceNgsVoiceDefGetPitchShiftBuss },
    };
    for (unsigned int i = 0; i < sizeof(defs) / sizeof(defs[0]); i++) {
        const SceNgsVoiceDefinition *def = defs[i].get();
        PRINTF(def != NULL, "%s: has a definition", defs[i].name);
        PRINTF(def == defs[i].get(), "%s: the same one each time", defs[i].name);
        for (int channels = 1; channels <= 2; channels++) {
            const SceNgsRackDescription desc = { def, 2, channels, 2, 2, NULL };
            SceSize size = 0x55555555;
            const int ret = sceNgsRackGetRequiredMemorySize(system, &desc, &size);
            PRINTF(ret == 0 ? (int)size : ret, "%s: rack memory for 2 voices of %d channels", defs[i].name, channels);
        }
    }

    // The rack params it checks
    static const struct {
        const char *what;
        int voices, channels, input_patches, output_patches;
    } racks_params[] = {
        { "0 voices", 0, 2, 1, 1 },
        { "9 voices", 9, 2, 1, 1 },
        { "0 channels", 1, 0, 1, 1 },
        { "3 channels", 1, 3, 1, 1 },
        { "no input patches", 1, 2, 0, 1 },
        { "no output patches", 1, 2, 1, 0 },
        { "-1 output patches", 1, 2, 1, -1 },
    };
    for (unsigned int i = 0; i < sizeof(racks_params) / sizeof(racks_params[0]); i++) {
        for (int d = 0; d < 2; d++) {
            const SceNgsRackDescription desc = {
                d ? sceNgsVoiceDefGetSimpleVoice() : sceNgsVoiceDefGetMasterBuss(), racks_params[i].voices, racks_params[i].channels,
                racks_params[i].input_patches, racks_params[i].output_patches, NULL,
            };
            SceSize size = 0x55555555;
            const int ret = sceNgsRackGetRequiredMemorySize(system, &desc, &size);
            PRINTF(ret == 0 ? (int)size : ret, "%s: rack memory with %s", d ? "simple voice" : "master buss", racks_params[i].what);
        }
    }

    // A master buss and a rack of two simple voices
    const SceNgsRackDescription master_desc = { sceNgsVoiceDefGetMasterBuss(), 1, 2, 4, 0, NULL };
    const SceNgsRackDescription voice_desc = { sceNgsVoiceDefGetSimpleVoice(), 2, 2, 0, 1, NULL };
    SceSize master_size = 0, voice_size = 0;
    sceNgsRackGetRequiredMemorySize(system, &master_desc, &master_size);
    sceNgsRackGetRequiredMemorySize(system, &voice_desc, &voice_size);
    SceNgsBufferInfo master_buffer = { take(master_size), master_size };
    SceNgsBufferInfo voice_buffer = { take(voice_size), voice_size };
    SceNgsHRack master_rack = 0, voice_rack = 0;
    SceNgsBufferInfo small = { voice_buffer.data, voice_size - 1 };
    PRINT("init the voice rack in a byte less than it needs", sceNgsRackInit(system, &small, &voice_desc, &voice_rack));
    PRINT("init the master rack", sceNgsRackInit(system, &master_buffer, &master_desc, &master_rack));
    PRINT("init the voice rack", sceNgsRackInit(system, &voice_buffer, &voice_desc, &voice_rack));
    PRINT("init a rack in an unknown system", sceNgsRackInit(0x12345, &voice_buffer, &voice_desc, &voice_rack));
    SceNgsHVoice master = 0, voice0 = 0, voice1 = 0, voice2 = 0x55555555;
    PRINT("get the master voice", sceNgsRackGetVoiceHandle(master_rack, 0, &master));
    PRINT("get voice 0", sceNgsRackGetVoiceHandle(voice_rack, 0, &voice0));
    PRINT("get voice 1", sceNgsRackGetVoiceHandle(voice_rack, 1, &voice1));
    PRINT("get voice 2 of 2", sceNgsRackGetVoiceHandle(voice_rack, 2, &voice2));
    PRINT("voice 2 is left alone", voice2 == 0x55555555);
    PRINT("the voices differ", voice0 != voice1 && voice0 != master);
    print_voice_info("master", master);
    print_voice_info("voice 0", voice0);

    // Their modules
    SceNgsVoiceInfo info = { 0 };
    sceNgsVoiceGetInfo(voice0, &info);
    for (SceUInt32 m = 0; m <= info.num_modules; m++) {
        SceNgsModuleID type = 0x55555555;
        const int ret = sceNgsVoiceGetModuleType(voice0, m, &type);
        PRINTF(ret == 0 ? (int)type : ret, "voice 0: the type of module %u", m);
        SceUInt32 bypass = 0x55555555;
        const int bypass_ret = sceNgsVoiceGetModuleBypass(voice0, m, &bypass);
        PRINTF(bypass_ret == 0 ? (int)bypass : bypass_ret, "voice 0: the bypass of module %u", m);
    }
    sceNgsVoiceGetInfo(master, &info);
    for (SceUInt32 m = 0; m <= info.num_modules; m++) {
        SceNgsModuleID type = 0x55555555;
        const int ret = sceNgsVoiceGetModuleType(master, m, &type);
        PRINTF(ret == 0 ? (int)type : ret, "master: the type of module %u", m);
    }
    PRINT("bypass module 0 of voice 0", sceNgsVoiceBypassModule(voice0, 0, SCE_NGS_MODULE_FLAG_BYPASSED));
    SceUInt32 bypass = 0;
    sceNgsVoiceGetModuleBypass(voice0, 0, &bypass);
    PRINT("its bypass after that", bypass);
    PRINT("bypass it with flag 1", sceNgsVoiceBypassModule(voice0, 0, 1));
    PRINT("stop bypassing it", sceNgsVoiceBypassModule(voice0, 0, SCE_NGS_MODULE_FLAG_NOT_BYPASSED));

    // Patching the voices into the master buss
    SceNgsPatchSetupInfo setup = { voice0, 0, 0, master, 0 };
    SceNgsHPatch patch0 = 0, patch1 = 0, extra = 0x55555555;
    PRINT("patch voice 0 into the master", sceNgsPatchCreateRouting(&setup, &patch0));
    PRINT("patch voice 0 into it again", sceNgsPatchCreateRouting(&setup, &extra));
    setup.source_voice = voice1;
    setup.source_output_sub_index = -1;
    PRINT("patch voice 1 into the first free slot", sceNgsPatchCreateRouting(&setup, &patch1));
    setup.source_output_index = 1;
    PRINT("patch from output 1", sceNgsPatchCreateRouting(&setup, &extra));
    setup.source_output_index = 0;
    setup.destination_input_index = 1;
    PRINT("patch into input 1", sceNgsPatchCreateRouting(&setup, &extra));
    setup.destination_voice = voice0;
    setup.destination_input_index = 0;
    PRINT("patch into a voice without inputs", sceNgsPatchCreateRouting(&setup, &extra));
    SceNgsHPatch found = 0;
    PRINT("get voice 0's output patch", sceNgsVoiceGetOutputPatch(voice0, 0, 0, &found));
    PRINT("it is the patch", found == patch0);
    PRINT("get voice 0's output patch 1", sceNgsVoiceGetOutputPatch(voice0, 0, 1, &found));
    SceNgsPatchRouteInfo route;
    SceNgsPatchSetupInfo got;
    sceClibMemset(&route, 0x55, sizeof(route));
    sceClibMemset(&got, 0x55, sizeof(got));
    PRINT("get the patch info", sceNgsPatchGetInfo(patch0, &route, &got));
    PRINT("its output channels", route.output_channels);
    PRINT("its input channels", route.input_channels);
    for (int o = 0; o < 2; o++) {
        for (int i = 0; i < 2; i++)
            PRINTF(float_bits(route.volume_matrix.matrix[o][i]), "its volume from %d to %d", o, i);
    }
    PRINT("its setup's source", got.source_voice == voice0 && got.destination_voice == master);
    PRINT("its setup's indices", got.source_output_index | got.source_output_sub_index << 8 | got.destination_input_index << 16);
    PRINT("set a volume", sceNgsVoicePatchSetVolume(patch0, 0, 1, 0.5f));
    PRINT("set a volume of channel 2", sceNgsVoicePatchSetVolume(patch0, 2, 0, 0.5f));
    const float volumes[2] = { 0.25f, 2.0f };
    PRINT("set the volumes of a channel", sceNgsVoicePatchSetVolumes(patch0, 1, volumes, 2));
    PRINT("set 3 volumes of a channel", sceNgsVoicePatchSetVolumes(patch0, 1, volumes, 3));
    const SceNgsVolumeMatrix matrix = { { { 1.0f, 0.0f }, { -1.0f, 8.0f } } };
    PRINT("set the matrix", sceNgsVoicePatchSetVolumesMatrix(patch0, &matrix));
    sceNgsPatchGetInfo(patch0, &route, &got);
    for (int o = 0; o < 2; o++) {
        for (int i = 0; i < 2; i++)
            PRINTF(float_bits(route.volume_matrix.matrix[o][i]), "its volume from %d to %d after that", o, i);
    }

    // Voice states without updates, which ngs_update tests
    PRINT("play the master", sceNgsVoicePlay(master));
    PRINT("play voice 0", sceNgsVoicePlay(voice0));
    print_voice_info("voice 0 after play", voice0);
    PRINT("pause voice 1", sceNgsVoicePause(voice1));
    PRINT("resume voice 1", sceNgsVoiceResume(voice1));
    PRINT("play voice 1", sceNgsVoicePlay(voice1));
    PRINT("pause voice 1 after that", sceNgsVoicePause(voice1));
    print_voice_info("voice 1 paused", voice1);
    PRINT("pause it again", sceNgsVoicePause(voice1));
    PRINT("resume it", sceNgsVoiceResume(voice1));
    PRINT("key off voice 1", sceNgsVoiceKeyOff(voice1));
    print_voice_info("voice 1 after key off", voice1);
    PRINT("kill voice 0", sceNgsVoiceKill(voice0));
    print_voice_info("voice 0 after kill", voice0);
    PRINT("kill it again", sceNgsVoiceKill(voice0));
    PRINT("init voice 0 with everything", sceNgsVoiceInit(voice0, NULL, SCE_NGS_VOICE_INIT_ALL));
    print_voice_info("voice 0 after init", voice0);
    PRINT("voice 0's patch after init", sceNgsVoiceGetOutputPatch(voice0, 0, 0, &found));
    PRINT("init voice 0 with flag 8", sceNgsVoiceInit(voice0, NULL, 8));

    // Removing patches and releasing, out of order first
    PRINT("remove voice 1's patch", sceNgsPatchRemoveRouting(patch1));
    PRINT("remove it again", sceNgsPatchRemoveRouting(patch1));
    PRINT("remove an unknown patch", sceNgsPatchRemoveRouting(0x12345));
    PRINT("release the system with its racks", sceNgsSystemRelease(system));
    PRINT("release the master rack with a voice patched in", sceNgsRackRelease(master_rack, NULL));
    PRINT("release the voice rack", sceNgsRackRelease(voice_rack, NULL));
    PRINT("release it again", sceNgsRackRelease(voice_rack, NULL));
    PRINT("get a voice of the released rack", sceNgsRackGetVoiceHandle(voice_rack, 0, &found));
    PRINT("get the info of a released voice", sceNgsVoiceGetInfo(voice1, &info));
    sceNgsRackRelease(master_rack, NULL);
    PRINT("release the system", sceNgsSystemRelease(system));
    PRINT("release it again", sceNgsSystemRelease(system));
    PRINT("release an unknown system", sceNgsSystemRelease(0x12345));

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    SceSize size = 0;
    PRINT("system memory with NULL params", sceNgsSystemGetRequiredMemorySize(NULL, &size));
    PRINT("system memory into NULL", sceNgsSystemGetRequiredMemorySize(&base_params, NULL));
    PRINT("init a system in NULL", sceNgsSystemInit(NULL, system_size, &base_params, &system));
    PRINT("init a system", sceNgsSystemInit(system_memory, system_size, &base_params, &system));
    const SceNgsRackDescription no_def = { NULL, 1, 2, 1, 1, NULL };
    PRINT("rack memory without a definition", sceNgsRackGetRequiredMemorySize(system, &no_def, &size));
    PRINT("rack memory with a NULL description", sceNgsRackGetRequiredMemorySize(system, NULL, &size));
    PRINT("init a rack with a NULL buffer", sceNgsRackInit(system, NULL, &voice_desc, &voice_rack));
    PRINT("get a voice into NULL", sceNgsRackGetVoiceHandle(voice_rack, 0, NULL));
    PRINT("create a patch with NULL setup", sceNgsPatchCreateRouting(NULL, &patch0));
    sceNgsSystemRelease(system);
    PRINT("unload the ngs module", sceSysmoduleUnloadModule(SCE_SYSMODULE_NGS));

    sceKernelFreeMemBlock(block);
    test_exit();
    return 0;
}
