// Audio output ports: which types, lengths, frequencies and modes a port takes, reading and changing
// its config, volumes, how many ports can be open, and releasing them. Nothing is played.
#include <psp2/audioout.h>

#include "test.h"

// Returns 0 if a port opens like this, or the error, and releases it
static int open_result(int type, int len, int freq, int mode) {
    const int port = sceAudioOutOpenPort(type, len, freq, mode);
    if (port < 0)
        return port;
    sceAudioOutReleasePort(port);
    return 0;
}

int main(void) {
    // Types, lengths, frequencies and modes
    static const char *const types[] = { "main", "bgm", "voice" };
    for (int type = 0; type < 3; type++) {
        PRINTF(open_result(type, 256, 48000, SCE_AUDIO_OUT_MODE_STEREO), "%s: open stereo at 48 kHz", types[type]);
        PRINTF(open_result(type, 256, 48000, SCE_AUDIO_OUT_MODE_MONO), "%s: open mono at 48 kHz", types[type]);
        static const int freqs[] = { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 96000 };
        for (unsigned int i = 0; i < sizeof(freqs) / sizeof(freqs[0]); i++)
            PRINTF(open_result(type, 256, freqs[i], SCE_AUDIO_OUT_MODE_STEREO), "%s: open at %d Hz", types[type], freqs[i]);
        static const int lens[] = { 0, 63, 64, 65, 256, 4096, 65472, 65536 };
        for (unsigned int i = 0; i < sizeof(lens) / sizeof(lens[0]); i++)
            PRINTF(open_result(type, lens[i], 48000, SCE_AUDIO_OUT_MODE_STEREO), "%s: open with %d samples", types[type], lens[i]);
    }
    PRINT("open type 3", open_result(3, 256, 48000, SCE_AUDIO_OUT_MODE_STEREO));
    PRINT("open type -1", open_result(-1, 256, 48000, SCE_AUDIO_OUT_MODE_STEREO));
    PRINT("open mode 2", open_result(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 256, 48000, 2));

    // One port's config
    const int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 256, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    PRINT("open a port", port >= 0);
    PRINT("its length", sceAudioOutGetConfig(port, SCE_AUDIO_OUT_CONFIG_TYPE_LEN));
    PRINT("its frequency", sceAudioOutGetConfig(port, SCE_AUDIO_OUT_CONFIG_TYPE_FREQ));
    PRINT("its mode", sceAudioOutGetConfig(port, SCE_AUDIO_OUT_CONFIG_TYPE_MODE));
    PRINT("config type 3", sceAudioOutGetConfig(port, 3));
    PRINT("the samples left to play", sceAudioOutGetRestSample(port));
    PRINT("set the length to 512 and keep the rest", sceAudioOutSetConfig(port, 512, -1, -1));
    PRINT("its length after it", sceAudioOutGetConfig(port, SCE_AUDIO_OUT_CONFIG_TYPE_LEN));
    PRINT("its frequency after it", sceAudioOutGetConfig(port, SCE_AUDIO_OUT_CONFIG_TYPE_FREQ));
    PRINT("set the frequency to 44.1 kHz", sceAudioOutSetConfig(port, -1, 44100, -1));
    PRINT("its frequency after that", sceAudioOutGetConfig(port, SCE_AUDIO_OUT_CONFIG_TYPE_FREQ));
    PRINT("set mono", sceAudioOutSetConfig(port, -1, -1, SCE_AUDIO_OUT_MODE_MONO));
    PRINT("its mode after that", sceAudioOutGetConfig(port, SCE_AUDIO_OUT_CONFIG_TYPE_MODE));
    PRINT("set a length of 63", sceAudioOutSetConfig(port, 63, -1, -1));
    PRINT("set a frequency of 12345", sceAudioOutSetConfig(port, -1, 12345, -1));

    // Volumes
    int volume[2] = { 32768, 32768 };
    PRINT("set both volumes to the maximum", sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, volume));
    volume[0] = 32769;
    PRINT("set a volume past the maximum", sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH, volume));
    volume[0] = -1;
    PRINT("set a negative volume", sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH, volume));
    volume[0] = 0;
    PRINT("set a volume with no channel", sceAudioOutSetVolume(port, 0, volume));
    PRINT("set a volume with channel flag 4", sceAudioOutSetVolume(port, 4, volume));

    // Releasing
    PRINT("release it", sceAudioOutReleasePort(port));
    PRINT("release it again", sceAudioOutReleasePort(port));
    PRINT("the config of a released port", sceAudioOutGetConfig(port, SCE_AUDIO_OUT_CONFIG_TYPE_LEN));
    PRINT("release an unknown port", sceAudioOutReleasePort(0x12345));
    PRINT("release port -1", sceAudioOutReleasePort(-1));

    // How many ports of each type can be open at once
    for (int type = 0; type < 3; type++) {
        int ports[16];
        int count = 0;
        while (count < 16 && (ports[count] = sceAudioOutOpenPort(type, 256, 48000, SCE_AUDIO_OUT_MODE_STEREO)) >= 0)
            count++;
        PRINTF(count, "%s: ports open at once", types[type]);
        PRINTF(count < 16 ? ports[count] : 0, "%s: the error after the last", types[type]);
        for (int i = 0; i < count; i++)
            sceAudioOutReleasePort(ports[i]);
    }

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    const int last = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 256, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    PRINT("set a volume from a NULL pointer", sceAudioOutSetVolume(last, SCE_AUDIO_VOLUME_FLAG_L_CH, NULL));
    sceAudioOutReleasePort(last);

    test_exit();
    return 0;
}
