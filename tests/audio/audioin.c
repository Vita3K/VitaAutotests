// Audio input ports: which types, grains, frequencies and params a port takes, how many open at once,
// reading a grain and how long it blocks, mic gain, and the status calls. What the mic hears isn't
// printed, only whether a read filled the buffer it was given.
#include <psp2/audioin.h>

#include "test.h"

static short samples[4096 + 64] __attribute__((aligned(64)));

static int open_result(int type, int grain, int freq, int param) {
    const int port = sceAudioInOpenPort(type, grain, freq, param);
    if (port < 0)
        return port;
    sceAudioInReleasePort(port);
    return 0;
}

int main(void) {
    // Types
    static const int types[] = { SCE_AUDIO_IN_PORT_TYPE_VOICE, 1, SCE_AUDIO_IN_PORT_TYPE_RAW, 3, SCE_AUDIO_IN_PORT_TYPE_DIAG, SCE_AUDIO_IN_PORT_TYPE_CAMERA, -1 };
    for (unsigned int i = 0; i < sizeof(types) / sizeof(types[0]); i++)
        PRINTF(open_result(types[i], 256, 16000, SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO), "open type %d", types[i]);

    // Grains and frequencies of the voice and raw types
    static const int grains[] = { 0, 1, 64, 128, 160, 255, 256, 320, 512, 1024, 2048, 4096, 65536, -256 };
    static const int freqs[] = { 8000, 11025, 16000, 22050, 44100, 48000, 0 };
    static const int grid_types[] = { SCE_AUDIO_IN_PORT_TYPE_VOICE, SCE_AUDIO_IN_PORT_TYPE_RAW };
    for (int t = 0; t < 2; t++) {
        for (unsigned int g = 0; g < sizeof(grains) / sizeof(grains[0]); g++)
            PRINTF(open_result(grid_types[t], grains[g], 16000, SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO), "type %d: open with a grain of %d", grid_types[t], grains[g]);
        for (unsigned int f = 0; f < sizeof(freqs) / sizeof(freqs[0]); f++)
            PRINTF(open_result(grid_types[t], 256, freqs[f], SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO), "type %d: open at %d Hz", grid_types[t], freqs[f]);
        for (unsigned int f = 0; f < sizeof(freqs) / sizeof(freqs[0]) - 1; f++)
            PRINTF(open_result(grid_types[t], 160, freqs[f], SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO), "type %d: open with a grain of 160 at %d Hz", grid_types[t], freqs[f]);
        PRINTF(open_result(grid_types[t], 256, 16000, 1), "type %d: open with param 1", grid_types[t]);
        PRINTF(open_result(grid_types[t], 256, 16000, -1), "type %d: open with param -1", grid_types[t]);
    }
    PRINT("open with a bad type, grain, frequency and param", open_result(7, 1, 12345, 9));
    PRINT("open with a bad grain, frequency and param", open_result(SCE_AUDIO_IN_PORT_TYPE_VOICE, 1, 12345, 9));
    PRINT("open with a bad frequency and param", open_result(SCE_AUDIO_IN_PORT_TYPE_VOICE, 256, 12345, 9));
    PRINT("open for diag with the diag type", open_result(SCE_AUDIO_IN_PORT_TYPE_DIAG, 256, 16000, SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO));
    const int diag = sceAudioInOpenPortForDiag(SCE_AUDIO_IN_PORT_TYPE_DIAG, 256, 16000, SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);
    PRINT("open a port for diag", diag < 0 ? diag : 0);
    if (diag >= 0)
        sceAudioInReleasePort(diag);

    // One port
    const int port = sceAudioInOpenPort(SCE_AUDIO_IN_PORT_TYPE_VOICE, 256, 16000, SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);
    PRINT("open a voice port", port < 0 ? port : 0);
    PRINT("open another voice port", open_result(SCE_AUDIO_IN_PORT_TYPE_VOICE, 256, 16000, SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO));
    PRINT("open a raw port beside it", open_result(SCE_AUDIO_IN_PORT_TYPE_RAW, 256, 16000, SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO));
    PRINT("adopt of the voice type", sceAudioInGetAdopt(SCE_AUDIO_IN_PORT_TYPE_VOICE));
    PRINT("adopt of the raw type", sceAudioInGetAdopt(SCE_AUDIO_IN_PORT_TYPE_RAW));
    PRINT("adopt of type 1", sceAudioInGetAdopt(1));
    PRINT("the mute status", sceAudioInGetStatus(SCE_AUDIO_IN_GETSTATUS_MUTE));
    PRINT("status 0", sceAudioInGetStatus(0));
    PRINT("status 2", sceAudioInGetStatus(2));
    PRINT("the input it reads from", sceAudioInGetInput());
    const int gain = sceAudioInGetMicGain(port);
    PRINT("get the mic gain", gain < 0 ? gain : 0);
    PRINT("get the mic gain of an unknown port", sceAudioInGetMicGain(0x12345));

    // Reading, which fills the grain and blocks for about its time: 16 grains of 256 at 16 kHz are 256 ms
    sceClibMemset(samples, 0x5A, sizeof(samples));
    PRINT("read a grain", sceAudioInInput(port, samples));
    PRINT("it writes no further than the grain", samples[256] == 0x5A5A && samples[256 + 63] == 0x5A5A);
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    int reads = 0;
    for (int i = 0; i < 16; i++)
        reads |= sceAudioInInput(port, samples);
    const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - start;
    PRINT("read 16 more grains", reads);
    PRINT("they take at least 192 ms", elapsed >= 192000);
    PRINT("and at most 320 ms", elapsed <= 320000);
    int device_state = 0x55;
    PRINT("read with the input device state", sceAudioInInputWithInputDeviceState(port, samples, &device_state));
    PRINT("the device state was written", device_state != 0x55);
    PRINT("read into a buffer 1 byte past alignment", sceAudioInInput(port, (char *)samples + 1));
    PRINT("read into a buffer 2 bytes past alignment", sceAudioInInput(port, (char *)samples + 2));
    PRINT("read into NULL", sceAudioInInput(port, NULL));
    PRINT("read with the state into NULL", sceAudioInInputWithInputDeviceState(port, samples, NULL));

    // Releasing
    PRINT("release it", sceAudioInReleasePort(port));
    PRINT("release it again", sceAudioInReleasePort(port));
    PRINT("read from the released port", sceAudioInInput(port, samples));
    PRINT("get the mic gain of the released port", sceAudioInGetMicGain(port));
    PRINT("release an unknown port", sceAudioInReleasePort(0x12345));
    PRINT("release port -1", sceAudioInReleasePort(-1));
    PRINT("open a voice port again", open_result(SCE_AUDIO_IN_PORT_TYPE_VOICE, 256, 16000, SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO));

    // A raw port at 48 kHz: 16 grains of 512 are about 171 ms
    const int raw = sceAudioInOpenPort(SCE_AUDIO_IN_PORT_TYPE_RAW, 512, 48000, SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);
    PRINT("open a raw port at 48 kHz", raw < 0 ? raw : 0);
    if (raw >= 0) {
        sceAudioInInput(raw, samples);
        const SceUInt64 raw_start = sceKernelGetProcessTimeWide();
        int raw_reads = 0;
        for (int i = 0; i < 16; i++)
            raw_reads |= sceAudioInInput(raw, samples);
        const SceUInt64 raw_elapsed = sceKernelGetProcessTimeWide() - raw_start;
        PRINT("read 16 grains", raw_reads);
        PRINT("they take at least 128 ms", raw_elapsed >= 128000);
        PRINT("and at most 214 ms", raw_elapsed <= 214000);
        sceAudioInReleasePort(raw);
    }

    test_exit();
    return 0;
}
