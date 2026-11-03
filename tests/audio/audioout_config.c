// Audio output ports, past what audioout covers: each length with each frequency, changing the config
// of each port type, the ext port, ports of every type open together, and the system-wide ALC,
// effect, compress and adopt settings. Nothing is played.
#include <psp2/audioout.h>

#include "test.h"

static int open_result(int type, int len, int freq, int mode) {
    const int port = sceAudioOutOpenPort(type, len, freq, mode);
    if (port < 0)
        return port;
    sceAudioOutReleasePort(port);
    return 0;
}

static const char *const types[] = { "main", "bgm", "voice" };

int main(void) {
    // Each length with each frequency, on the bgm port, which takes the most frequencies
    static const int lens[] = { 64, 128, 192, 320, 480, 1024, 2048, 4032, 65408, 65472 };
    static const int freqs[] = { 8000, 11025, 22050, 44100, 44184, 48000 };
    for (unsigned int l = 0; l < sizeof(lens) / sizeof(lens[0]); l++) {
        for (unsigned int f = 0; f < sizeof(freqs) / sizeof(freqs[0]); f++)
            PRINTF(open_result(SCE_AUDIO_OUT_PORT_TYPE_BGM, lens[l], freqs[f], SCE_AUDIO_OUT_MODE_MONO), "bgm: open with %d samples at %d Hz", lens[l], freqs[f]);
    }
    // Lengths that aren't a multiple of 64, and negative ones
    static const int odd_lens[] = { 1, 32, 100, 127, 129, 255, 4095, 65471, -64, -1 };
    for (unsigned int l = 0; l < sizeof(odd_lens) / sizeof(odd_lens[0]); l++)
        PRINTF(open_result(SCE_AUDIO_OUT_PORT_TYPE_BGM, odd_lens[l], 48000, SCE_AUDIO_OUT_MODE_STEREO), "bgm: open with %d samples", odd_lens[l]);
    // Frequencies near the ones it takes
    static const int odd_freqs[] = { 0, -48000, 7999, 8001, 44099, 47999, 48001, 192000 };
    for (unsigned int f = 0; f < sizeof(odd_freqs) / sizeof(odd_freqs[0]); f++)
        PRINTF(open_result(SCE_AUDIO_OUT_PORT_TYPE_BGM, 256, odd_freqs[f], SCE_AUDIO_OUT_MODE_STEREO), "bgm: open at %d Hz", odd_freqs[f]);
    PRINT("main: open at 44184 Hz", open_result(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 256, 44184, SCE_AUDIO_OUT_MODE_STEREO));
    PRINT("voice: open at 44184 Hz", open_result(SCE_AUDIO_OUT_PORT_TYPE_VOICE, 256, 44184, SCE_AUDIO_OUT_MODE_STEREO));
    // Which error goes first when more than one argument is wrong
    PRINT("open with a bad type, length, frequency and mode", open_result(7, 63, 12345, 5));
    PRINT("open with a bad length, frequency and mode", open_result(SCE_AUDIO_OUT_PORT_TYPE_BGM, 63, 12345, 5));
    PRINT("open with a bad frequency and mode", open_result(SCE_AUDIO_OUT_PORT_TYPE_BGM, 256, 12345, 5));
    PRINT("open with a bad length and frequency", open_result(SCE_AUDIO_OUT_PORT_TYPE_BGM, 63, 12345, SCE_AUDIO_OUT_MODE_STEREO));
    PRINT("open with a bad length and mode", open_result(SCE_AUDIO_OUT_PORT_TYPE_BGM, 63, 48000, 5));
    PRINT("open the compressed voice type", open_result(SCE_AUDIO_OUT_PORT_TYPE_VOICE_COMPRESSED, 256, 48000, SCE_AUDIO_OUT_MODE_STEREO));

    // Changing the config of each type, a field at a time and all at once
    for (int type = 0; type < 3; type++) {
        const int freq = 48000;
        const int port = sceAudioOutOpenPort(type, 256, freq, SCE_AUDIO_OUT_MODE_STEREO);
        PRINTF(port >= 0, "%s: open a port", types[type]);
        PRINTF(sceAudioOutSetConfig(port, -1, -1, -1), "%s: set nothing", types[type]);
        PRINTF(sceAudioOutSetConfig(port, 1024, -1, -1), "%s: set a length of 1024", types[type]);
        PRINTF(sceAudioOutGetConfig(port, SCE_AUDIO_OUT_CONFIG_TYPE_LEN), "%s: its length", types[type]);
        PRINTF(sceAudioOutSetConfig(port, -1, 48000, -1), "%s: set the frequency it has", types[type]);
        PRINTF(sceAudioOutSetConfig(port, -1, 16000, -1), "%s: set 16 kHz", types[type]);
        PRINTF(sceAudioOutGetConfig(port, SCE_AUDIO_OUT_CONFIG_TYPE_FREQ), "%s: its frequency", types[type]);
        PRINTF(sceAudioOutSetConfig(port, -1, -1, SCE_AUDIO_OUT_MODE_MONO), "%s: set mono", types[type]);
        PRINTF(sceAudioOutSetConfig(port, -1, -1, 2), "%s: set mode 2", types[type]);
        PRINTF(sceAudioOutGetConfig(port, SCE_AUDIO_OUT_CONFIG_TYPE_MODE), "%s: its mode", types[type]);
        PRINTF(sceAudioOutSetConfig(port, 512, 48000, SCE_AUDIO_OUT_MODE_STEREO), "%s: set all three", types[type]);
        PRINTF(sceAudioOutGetConfig(port, SCE_AUDIO_OUT_CONFIG_TYPE_LEN), "%s: its length after that", types[type]);
        PRINTF(sceAudioOutGetConfig(port, SCE_AUDIO_OUT_CONFIG_TYPE_MODE), "%s: its mode after that", types[type]);
        PRINTF(sceAudioOutSetConfig(port, 0, -1, -1), "%s: set a length of 0", types[type]);
        PRINTF(sceAudioOutSetConfig(port, 65536, -1, -1), "%s: set a length of 65536", types[type]);
        PRINTF(sceAudioOutSetConfig(port, 63, 12345, 5), "%s: set a bad length, frequency and mode", types[type]);
        PRINTF(sceAudioOutGetConfig(port, -1), "%s: config type -1", types[type]);
        PRINTF(sceAudioOutReleasePort(port), "%s: release it", types[type]);
        PRINTF(sceAudioOutSetConfig(port, 256, -1, -1), "%s: set the config of the released port", types[type]);
    }

    // The ext port
    const int ext = sceAudioOutOpenExtPort(256, SCE_AUDIO_OUT_MODE_STEREO);
    PRINT("open the ext port", ext < 0 ? ext : 0);
    if (ext >= 0) {
        PRINT("the ext port's length", sceAudioOutGetConfig(ext, SCE_AUDIO_OUT_CONFIG_TYPE_LEN));
        PRINT("the ext port's frequency", sceAudioOutGetConfig(ext, SCE_AUDIO_OUT_CONFIG_TYPE_FREQ));
        PRINT("release the ext port", sceAudioOutReleasePort(ext));
    }
    const int ext_odd = sceAudioOutOpenExtPort(63, SCE_AUDIO_OUT_MODE_STEREO);
    PRINT("open the ext port with 63 samples", ext_odd < 0 ? ext_odd : 0);
    if (ext_odd >= 0)
        sceAudioOutReleasePort(ext_odd);

    // Ports of every type open together, and whether a released slot opens again
    int ports[10], count = 0;
    for (int type = 0; type < 3; type++) {
        while (count < 10 && (ports[count] = sceAudioOutOpenPort(type, 256, 48000, SCE_AUDIO_OUT_MODE_STEREO)) >= 0)
            count++;
    }
    PRINT("ports of all types open at once", count);
    PRINT("the main ports have different IDs", count >= 2 && ports[0] != ports[1]);
    sceAudioOutReleasePort(ports[0]);
    const int again = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 256, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    PRINT("open a main port after releasing one", again >= 0);
    PRINT("it gets the released ID back", again == ports[0]);
    ports[0] = again;
    for (int i = 0; i < count; i++)
        sceAudioOutReleasePort(ports[i]);

    // Adoption, per type
    static const int adopt_types[] = { 0, 1, 2, 3, 4, -1 };
    for (unsigned int i = 0; i < sizeof(adopt_types) / sizeof(adopt_types[0]); i++)
        PRINTF(sceAudioOutGetAdopt(adopt_types[i]), "adopt of type %d", adopt_types[i]);

    // System-wide settings, left as they were found where it can tell
    static const int alc_modes[] = { SCE_AUDIO_ALC_MODE1, SCE_AUDIO_ALC_OFF, SCE_AUDIO_ALC_MODE_MAX, -1 };
    for (unsigned int i = 0; i < sizeof(alc_modes) / sizeof(alc_modes[0]); i++)
        PRINTF(sceAudioOutSetAlcMode(alc_modes[i]), "set ALC mode %d", alc_modes[i]);
    static const int effects[] = { SCE_AUDIO_OUT_EFFECT_TYPE_HEAVY, SCE_AUDIO_OUT_EFFECT_TYPE_UNIQUE, 5, -1, SCE_AUDIO_OUT_EFFECT_TYPE_OFF };
    for (unsigned int i = 0; i < sizeof(effects) / sizeof(effects[0]); i++)
        PRINTF(sceAudioOutSetEffectType(effects[i]), "set effect type %d", effects[i]);
    const int bgm = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 256, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    const int main_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 256, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    PRINT("compress a bgm port", sceAudioOutSetCompress(bgm, 1));
    PRINT("stop compressing it", sceAudioOutSetCompress(bgm, 0));
    PRINT("compress it with 2", sceAudioOutSetCompress(bgm, 2));
    PRINT("compress a main port", sceAudioOutSetCompress(main_port, 1));
    sceAudioOutSetCompress(main_port, 0);
    PRINT("compress an unknown port", sceAudioOutSetCompress(0x12345, 1));
    sceAudioOutReleasePort(bgm);
    PRINT("compress the released port", sceAudioOutSetCompress(bgm, 1));
    sceAudioOutReleasePort(main_port);
    PRINT("set adopt mode manual", sceAudioOutSetAdoptMode(SCE_AUDIO_OUT_ADOPT_MODE_MANUAL));
    PRINT("set adopt mode 2", sceAudioOutSetAdoptMode(2));
    PRINT("set adopt mode automatic", sceAudioOutSetAdoptMode(SCE_AUDIO_OUT_ADOPT_MODE_AUTOMATIC));

    test_exit();
    return 0;
}
