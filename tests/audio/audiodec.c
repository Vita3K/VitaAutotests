// sceAudiodec: initializing each codec's library and the params it checks, decoders of each kind and
// what they report, and decoding MP3 frames of silence built here, MPEG-1 and MPEG-2, stereo and mono.
// A frame with all of its side info zero holds no samples, so it decodes to zeros.
#include <psp2/audiodec.h>

#include "test.h"

static SceUInt8 es[2048] __attribute__((aligned(SCE_AUDIODEC_ALIGNMENT_SIZE)));
static short pcm[SCE_AUDIODEC_MP3_MAX_SAMPLES * 2 + 256] __attribute__((aligned(SCE_AUDIODEC_ALIGNMENT_SIZE)));

// MPEG-1 Layer III at 128 kbps and 44.1 kHz, without padding or CRC: 417 bytes
#define MPEG1_FRAME 417
// MPEG-2 Layer III at 64 kbps and 22.05 kHz: 208 bytes
#define MPEG2_FRAME 208

// Writes count frames of silence with the given header to es, and zeros after them
static void build_frames(const SceUInt8 header[4], int frame_size, int count) {
    sceClibMemset(es, 0, sizeof(es));
    for (int i = 0; i < count; i++)
        sceClibMemcpy(es + i * frame_size, header, 4);
}

static const SceUInt8 mpeg1_stereo[4] = { 0xFF, 0xFB, 0x90, 0x00 };
static const SceUInt8 mpeg1_mono[4] = { 0xFF, 0xFB, 0x90, 0xC0 };
static const SceUInt8 mpeg2_stereo[4] = { 0xFF, 0xF3, 0x80, 0x00 };

static int init_streams(SceUInt32 type, SceUInt32 size, SceUInt32 streams) {
    SceAudiodecInitParam init = { 0 };
    init.mp3.size = size;
    init.mp3.totalStreams = streams;
    return sceAudiodecInitLibrary(type, &init);
}

static int init_at9(SceUInt32 size, SceUInt32 channels) {
    SceAudiodecInitParam init = { 0 };
    init.at9.size = size;
    init.at9.totalCh = channels;
    return sceAudiodecInitLibrary(SCE_AUDIODEC_TYPE_AT9, &init);
}

static SceAudiodecInfo info;
static SceAudiodecCtrl ctrl;

static void setup_mp3(SceUInt32 channels, SceUInt32 version) {
    sceClibMemset(&info, 0, sizeof(info));
    info.mp3.size = sizeof(info.mp3);
    info.mp3.ch = channels;
    info.mp3.version = version;
    sceClibMemset(&ctrl, 0, sizeof(ctrl));
    ctrl.size = sizeof(ctrl);
    ctrl.wordLength = SCE_AUDIODEC_WORD_LENGTH_16BITS;
    ctrl.pInfo = &info;
    ctrl.pEs = es;
    ctrl.pPcm = pcm;
}

// Decodes the frame at offset, and prints what it returns, how much it read and wrote, and whether
// the output is silence that stays in its size
static void decode(const char *what, int offset) {
    sceClibMemset(pcm, 0x5A, sizeof(pcm));
    ctrl.pEs = es + offset;
    ctrl.inputEsSize = 0x55555555;
    ctrl.outputPcmSize = 0x55555555;
    PRINTF(sceAudiodecDecode(&ctrl), "%s: decode", what);
    PRINTF(ctrl.inputEsSize, "%s: bytes read", what);
    PRINTF(ctrl.outputPcmSize, "%s: bytes written", what);
    int silent = 1;
    const SceUInt32 written = ctrl.outputPcmSize <= sizeof(pcm) ? ctrl.outputPcmSize : 0;
    for (SceUInt32 i = 0; i < written / 2; i++)
        silent &= pcm[i] == 0;
    PRINTF(silent, "%s: it is silence", what);
    PRINTF(written + 2 > sizeof(pcm) || pcm[written / 2] == 0x5A5A, "%s: nothing is written past it", what);
}

int main(void) {
    // Before the library is initialized
    setup_mp3(2, SCE_AUDIODEC_MP3_MPEG_VERSION_1);
    PRINT("create an mp3 decoder before init", sceAudiodecCreateDecoder(&ctrl, SCE_AUDIODEC_TYPE_MP3));
    PRINT("term mp3 before init", sceAudiodecTermLibrary(SCE_AUDIODEC_TYPE_MP3));

    // The codec types
    static const SceUInt32 bad_types[] = { 0, 0x1002, 0x1007, 0x2003, 0xFFFFFFFF };
    for (unsigned int i = 0; i < sizeof(bad_types) / sizeof(bad_types[0]); i++)
        PRINTF(init_streams(bad_types[i], sizeof(SceAudiodecInitStreamParam), 1), "init codec 0x%X", bad_types[i]);
    PRINTF(sceAudiodecTermLibrary(0x1007), "term codec 0x1007");

    // The params of the stream codecs
    static const struct {
        const char *name;
        SceUInt32 type;
    } stream_codecs[] = {
        { "mp3", SCE_AUDIODEC_TYPE_MP3 },
        { "aac", SCE_AUDIODEC_TYPE_AAC },
        { "celp", SCE_AUDIODEC_TYPE_CELP },
    };
    for (unsigned int c = 0; c < sizeof(stream_codecs) / sizeof(stream_codecs[0]); c++) {
        const char *name = stream_codecs[c].name;
        const SceUInt32 type = stream_codecs[c].type;
        PRINTF(init_streams(type, 0, 1), "%s: init with a size of 0", name);
        PRINTF(init_streams(type, 4, 1), "%s: init with a size of 4", name);
        PRINTF(init_streams(type, 12, 1), "%s: init with a size of 12", name);
        PRINTF(init_streams(type, 8, 0), "%s: init 0 streams", name);
        PRINTF(init_streams(type, 8, 9), "%s: init 9 streams", name);
        PRINTF(init_streams(type, 8, 8), "%s: init 8 streams", name);
        PRINTF(init_streams(type, 8, 1), "%s: init again", name);
        PRINTF(sceAudiodecTermLibrary(type), "%s: term", name);
        PRINTF(sceAudiodecTermLibrary(type), "%s: term again", name);
    }
    PRINT("at9: init with a size of 4", init_at9(4, 2));
    PRINT("at9: init 0 channels", init_at9(8, 0));
    PRINT("at9: init 17 channels", init_at9(8, 17));
    PRINT("at9: init 16 channels", init_at9(8, 16));
    PRINT("at9: init again", init_at9(8, 2));
    PRINT("at9: term", sceAudiodecTermLibrary(SCE_AUDIODEC_TYPE_AT9));
    PRINT("at9: init 1 channel", init_at9(8, 1));
    PRINT("at9: term that", sceAudiodecTermLibrary(SCE_AUDIODEC_TYPE_AT9));
    PRINT("init mp3 and aac together: mp3", init_streams(SCE_AUDIODEC_TYPE_MP3, 8, 2));
    PRINT("init mp3 and aac together: aac", init_streams(SCE_AUDIODEC_TYPE_AAC, 8, 2));
    PRINT("term aac while mp3 stays", sceAudiodecTermLibrary(SCE_AUDIODEC_TYPE_AAC));

    // Creating mp3 decoders, with mp3 initialized for 2 streams
    static const struct {
        const char *what;
        SceUInt32 ctrl_size, info_size, channels, version, word_length;
    } creates[] = {
        { "a control size of 0x24", 0x24, sizeof(SceAudiodecInfoMp3), 2, 3, 16 },
        { "a control size of 0x2C", 0x2C, sizeof(SceAudiodecInfoMp3), 2, 3, 16 },
        { "an info size of 8", sizeof(SceAudiodecCtrl), 8, 2, 3, 16 },
        { "an info size of 0x1C", sizeof(SceAudiodecCtrl), 0x1C, 2, 3, 16 },
        { "0 channels", sizeof(SceAudiodecCtrl), sizeof(SceAudiodecInfoMp3), 0, 3, 16 },
        { "3 channels", sizeof(SceAudiodecCtrl), sizeof(SceAudiodecInfoMp3), 3, 3, 16 },
        { "the reserved version", sizeof(SceAudiodecCtrl), sizeof(SceAudiodecInfoMp3), 2, 1, 16 },
        { "version 4", sizeof(SceAudiodecCtrl), sizeof(SceAudiodecInfoMp3), 2, 4, 16 },
        { "24 bit words", sizeof(SceAudiodecCtrl), sizeof(SceAudiodecInfoMp3), 2, 3, 24 },
        { "0 bit words", sizeof(SceAudiodecCtrl), sizeof(SceAudiodecInfoMp3), 2, 3, 0 },
    };
    for (unsigned int i = 0; i < sizeof(creates) / sizeof(creates[0]); i++) {
        setup_mp3(creates[i].channels, creates[i].version);
        ctrl.size = creates[i].ctrl_size;
        info.mp3.size = creates[i].info_size;
        ctrl.wordLength = creates[i].word_length;
        const int ret = sceAudiodecCreateDecoder(&ctrl, SCE_AUDIODEC_TYPE_MP3);
        PRINTF(ret, "create an mp3 decoder with %s", creates[i].what);
        if (ret >= 0)
            sceAudiodecDeleteDecoder(&ctrl);
    }
    setup_mp3(2, SCE_AUDIODEC_MP3_MPEG_VERSION_1);
    PRINT("create an aac decoder after aac's term", sceAudiodecCreateDecoder(&ctrl, SCE_AUDIODEC_TYPE_AAC));
    PRINT("the context size of an mpeg-1 stereo decoder", sceAudiodecGetContextSize(&ctrl, SCE_AUDIODEC_TYPE_MP3));
    setup_mp3(1, SCE_AUDIODEC_MP3_MPEG_VERSION_2);
    PRINT("the context size of an mpeg-2 mono decoder", sceAudiodecGetContextSize(&ctrl, SCE_AUDIODEC_TYPE_MP3));

    // MPEG-1 stereo
    setup_mp3(2, SCE_AUDIODEC_MP3_MPEG_VERSION_1);
    PRINT("create an mpeg-1 stereo decoder", sceAudiodecCreateDecoder(&ctrl, SCE_AUDIODEC_TYPE_MP3));
    PRINT("its handle is set", ctrl.handle != 0);
    PRINT("its largest input", ctrl.maxEsSize);
    PRINT("its largest output", ctrl.maxPcmSize);
    build_frames(mpeg1_stereo, MPEG1_FRAME, 3);
    decode("mpeg-1 stereo frame 1", 0);
    decode("mpeg-1 stereo frame 2", MPEG1_FRAME);
    PRINT("clear its context", sceAudiodecClearContext(&ctrl));
    decode("mpeg-1 stereo frame 3 after clearing", 2 * MPEG1_FRAME);
    ctrl.pEs = es;
    PRINT("decode 2 frames at once", sceAudiodecDecodeNFrames(&ctrl, 2));
    PRINT("decode 0 frames at once", sceAudiodecDecodeNFrames(&ctrl, 0));
    PRINT("decode 1 frame with DecodeNFrames", sceAudiodecDecodeNFrames(&ctrl, 1));
    PRINT("its bytes read", ctrl.inputEsSize);
    build_frames(mpeg2_stereo, MPEG2_FRAME, 1);
    decode("an mpeg-2 frame to the mpeg-1 decoder", 0);
    int internal = 0x55555555;
    PRINT("get the internal error", sceAudiodecGetInternalError(&ctrl, &internal));
    PRINT("the internal error", internal);
    sceClibMemset(es, 0, sizeof(es));
    decode("zeros", 0);
    internal = 0x55555555;
    sceAudiodecGetInternalError(&ctrl, &internal);
    PRINT("the internal error after the zeros", internal);
    build_frames(mpeg1_stereo, MPEG1_FRAME, 1);
    ctrl.maxEsSize = 100;
    decode("a frame with a largest input of 100", 0);
    ctrl.maxEsSize = SCE_AUDIODEC_MP3_MAX_ES_SIZE;
    ctrl.maxPcmSize = 1000;
    decode("a frame with a largest output of 1000", 0);
    ctrl.maxPcmSize = SCE_AUDIODEC_MP3_MAX_SAMPLES * 4;
    decode("a frame after putting the sizes back", 0);
    SceAudiodecCtrl stereo = ctrl;

    // A second decoder, and one more than the library was initialized for
    setup_mp3(1, SCE_AUDIODEC_MP3_MPEG_VERSION_1);
    PRINT("create an mpeg-1 mono decoder", sceAudiodecCreateDecoder(&ctrl, SCE_AUDIODEC_TYPE_MP3));
    PRINT("its largest output", ctrl.maxPcmSize);
    build_frames(mpeg1_mono, MPEG1_FRAME, 1);
    decode("mpeg-1 mono", 0);
    build_frames(mpeg1_stereo, MPEG1_FRAME, 1);
    decode("a stereo frame to the mono decoder", 0);
    SceAudiodecCtrl mono = ctrl;
    setup_mp3(2, SCE_AUDIODEC_MP3_MPEG_VERSION_2);
    PRINT("create a third decoder", sceAudiodecCreateDecoder(&ctrl, SCE_AUDIODEC_TYPE_MP3));
    PRINT("term mp3 with decoders left", sceAudiodecTermLibrary(SCE_AUDIODEC_TYPE_MP3));
    PRINT("delete the mono decoder", sceAudiodecDeleteDecoder(&mono));
    PRINT("delete it again", sceAudiodecDeleteDecoder(&mono));
    PRINT("decode with it", sceAudiodecDecode(&mono));

    // MPEG-2 stereo, in the freed slot
    setup_mp3(2, SCE_AUDIODEC_MP3_MPEG_VERSION_2);
    PRINT("create an mpeg-2 stereo decoder", sceAudiodecCreateDecoder(&ctrl, SCE_AUDIODEC_TYPE_MP3));
    PRINT("its largest output", ctrl.maxPcmSize);
    build_frames(mpeg2_stereo, MPEG2_FRAME, 2);
    decode("mpeg-2 stereo frame 1", 0);
    decode("mpeg-2 stereo frame 2", MPEG2_FRAME);
    SceAudiodecCtrl *both[2] = { &stereo, &ctrl };
    build_frames(mpeg2_stereo, MPEG2_FRAME, 1);
    PRINT("decode two streams of different versions at once", sceAudiodecDecodeNStreams(both, 2));
    SceAudiodecCtrl *same[2] = { &ctrl, &ctrl };
    PRINT("decode the same stream twice at once", sceAudiodecDecodeNStreams(same, 2));
    PRINT("decode 0 streams at once", sceAudiodecDecodeNStreams(both, 0));
    PRINT("delete it", sceAudiodecDeleteDecoder(&ctrl));
    PRINT("delete the mpeg-1 stereo decoder", sceAudiodecDeleteDecoder(&stereo));
    PRINT("term mp3", sceAudiodecTermLibrary(SCE_AUDIODEC_TYPE_MP3));
    PRINT("decode after the term", sceAudiodecDecode(&stereo));

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("init mp3 with NULL params", sceAudiodecInitLibrary(SCE_AUDIODEC_TYPE_MP3, NULL));
    init_streams(SCE_AUDIODEC_TYPE_MP3, 8, 1);
    PRINT("create a decoder with a NULL control", sceAudiodecCreateDecoder(NULL, SCE_AUDIODEC_TYPE_MP3));
    setup_mp3(2, SCE_AUDIODEC_MP3_MPEG_VERSION_1);
    ctrl.pInfo = NULL;
    PRINT("create a decoder with NULL info", sceAudiodecCreateDecoder(&ctrl, SCE_AUDIODEC_TYPE_MP3));
    setup_mp3(2, SCE_AUDIODEC_MP3_MPEG_VERSION_1);
    sceAudiodecCreateDecoder(&ctrl, SCE_AUDIODEC_TYPE_MP3);
    ctrl.pEs = NULL;
    PRINT("decode from NULL", sceAudiodecDecode(&ctrl));
    ctrl.pEs = es;
    ctrl.pPcm = NULL;
    PRINT("decode to NULL", sceAudiodecDecode(&ctrl));
    PRINT("get the internal error into NULL", sceAudiodecGetInternalError(&ctrl, NULL));
    PRINT("decode a NULL control", sceAudiodecDecode(NULL));
    sceAudiodecDeleteDecoder(&ctrl);
    sceAudiodecTermLibrary(SCE_AUDIODEC_TYPE_MP3);

    test_exit();
    return 0;
}
