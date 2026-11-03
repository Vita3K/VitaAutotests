// sceAudioOutOutput: how long it blocks for the samples a port plays at its frequency, the samples
// left after it, waiting for the end with a NULL buffer, buffer alignment, and a port another thread
// waits in. Only silence is played. Times are printed as comparisons far from each outcome.
#include <psp2/audioout.h>

#include "test.h"

static short silence[4096 * 2] __attribute__((aligned(64)));

// How long count outputs take on port, in microseconds
static SceUInt64 time_outputs(int port, int count) {
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    for (int i = 0; i < count; i++)
        sceAudioOutOutput(port, silence);
    return sceKernelGetProcessTimeWide() - start;
}

// Plays count buffers of len samples at freq, after two to fill what the port queues, and prints
// whether the time is within a quarter of what they take to play
static void print_timing(const char *what, int type, int len, int freq, int count) {
    const int port = sceAudioOutOpenPort(type, len, freq, SCE_AUDIO_OUT_MODE_STEREO);
    time_outputs(port, 2);
    const SceUInt64 elapsed = time_outputs(port, count);
    const SceUInt64 expected = (SceUInt64)count * len * 1000000 / freq;
    PRINTF(elapsed >= expected * 3 / 4, "%s: %d outputs take at least 3/4 of their play time", what, count);
    PRINTF(elapsed <= expected * 5 / 4, "%s: and at most 5/4 of it", what);
    sceAudioOutOutput(port, NULL);
    sceAudioOutReleasePort(port);
}

static int blocked_port;
static int blocked_results[4];

static int blocked_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (int i = 0; i < 4; i++)
        blocked_results[i] = sceAudioOutOutput(blocked_port, silence);
    return 0;
}

int main(void) {
    // One port, its first buffers and what is left to play
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 256, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    PRINT("open a bgm port", port >= 0);
    PRINT("output NULL with nothing queued", sceAudioOutOutput(port, NULL));
    PRINT("output a buffer", sceAudioOutOutput(port, silence));
    int rest = sceAudioOutGetRestSample(port);
    PRINT("the samples left after it are at most twice the length", rest >= 0 && rest <= 512);
    PRINT("output another", sceAudioOutOutput(port, silence));
    rest = sceAudioOutGetRestSample(port);
    PRINT("the samples left after that are at most twice the length", rest >= 0 && rest <= 512);
    PRINT("output NULL to wait for the end", sceAudioOutOutput(port, NULL));
    PRINT("the samples left after that", sceAudioOutGetRestSample(port));
    PRINT("output NULL again", sceAudioOutOutput(port, NULL));

    // Changing the port while it plays
    int volume[2] = { 16384, 8192 };
    sceAudioOutOutput(port, silence);
    PRINT("set the volume while it plays", sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, volume));
    PRINT("set mono while it plays", sceAudioOutSetConfig(port, -1, -1, SCE_AUDIO_OUT_MODE_MONO));
    PRINT("output a mono buffer", sceAudioOutOutput(port, silence));
    PRINT("set stereo again", sceAudioOutSetConfig(port, -1, -1, SCE_AUDIO_OUT_MODE_STEREO));
    PRINT("output a stereo buffer", sceAudioOutOutput(port, silence));
    sceAudioOutOutput(port, NULL);

    // Buffer alignment
    PRINT("output from a buffer 2 bytes past alignment", sceAudioOutOutput(port, (char *)silence + 2));
    PRINT("output from a buffer 1 byte past alignment", sceAudioOutOutput(port, (char *)silence + 1));
    PRINT("output from a buffer 4 bytes past alignment", sceAudioOutOutput(port, (char *)silence + 4));
    PRINT("output from a buffer 32 bytes past alignment", sceAudioOutOutput(port, (char *)silence + 32));
    sceAudioOutOutput(port, NULL);
    PRINT("release it", sceAudioOutReleasePort(port));
    PRINT("output to the released port", sceAudioOutOutput(port, silence));
    PRINT("output NULL to the released port", sceAudioOutOutput(port, NULL));
    PRINT("output to an unknown port", sceAudioOutOutput(0x12345, silence));
    PRINT("output to port -1", sceAudioOutOutput(-1, silence));

    // How long it blocks, at frequencies far enough apart that their times don't overlap
    print_timing("bgm with 256 samples at 48 kHz", SCE_AUDIO_OUT_PORT_TYPE_BGM, 256, 48000, 40);
    print_timing("bgm with 256 samples at 8 kHz", SCE_AUDIO_OUT_PORT_TYPE_BGM, 256, 8000, 12);
    print_timing("bgm with 1024 samples at 22.05 kHz", SCE_AUDIO_OUT_PORT_TYPE_BGM, 1024, 22050, 12);
    print_timing("bgm with 64 samples at 48 kHz", SCE_AUDIO_OUT_PORT_TYPE_BGM, 64, 48000, 100);
    print_timing("main with 256 samples at 48 kHz", SCE_AUDIO_OUT_PORT_TYPE_MAIN, 256, 48000, 40);
    print_timing("voice with 512 samples at 16 kHz", SCE_AUDIO_OUT_PORT_TYPE_VOICE, 512, 16000, 12);

    // A port another thread waits in, with buffers of half a second
    blocked_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 4096, 8000, SCE_AUDIO_OUT_MODE_STEREO);
    const SceUID thread = test_start_thread(blocked_entry);
    test_wait_until_waiting(thread);
    const int output_while_waiting = sceAudioOutOutput(blocked_port, silence);
    const int release_while_waiting = sceAudioOutReleasePort(blocked_port);
    test_join_thread(thread);
    PRINT("output to the port another thread waits in", output_while_waiting);
    PRINT("release the port another thread waits in", release_while_waiting);
    for (int i = 0; i < 4; i++)
        PRINTF(blocked_results[i], "the thread's output %d", i);
    PRINT("release the port after the thread ends", sceAudioOutReleasePort(blocked_port));

    // NULL with a port that never played
    port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_VOICE, 256, 48000, SCE_AUDIO_OUT_MODE_MONO);
    PRINT("output NULL to a voice port that never played", sceAudioOutOutput(port, NULL));
    PRINT("the samples it has left", sceAudioOutGetRestSample(port));
    sceAudioOutReleasePort(port);
    PRINT("the samples left on the released port", sceAudioOutGetRestSample(port));

    test_exit();
    return 0;
}
