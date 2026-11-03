// sceAtrac: the memory a decoder group needs, creating and deleting it and the audiodec library it
// initializes, and the files it takes a handle for: headers that aren't RIFF, WAVE or ATRAC9, sizes
// that don't fit, and an ATRAC9 header built here followed by frames of zeros, with what the handle
// reports about it.
#include <psp2/atrac.h>
#include <psp2/audiodec.h>
#include <psp2/sysmodule.h>

#include "test.h"

static SceUChar8 work[0x40000] __attribute__((aligned(SCE_ATRAC_ALIGNMENT_SIZE)));
static SceUChar8 file[0x2000] __attribute__((aligned(SCE_ATRAC_ALIGNMENT_SIZE)));
static short output[SCE_ATRAC_MAX_OUTPUT_SAMPLES * 2] __attribute__((aligned(SCE_ATRAC_ALIGNMENT_SIZE)));

static int query(SceUInt32 type, SceUInt32 size, SceUInt32 word_length, SceUInt32 channels) {
    const SceAtracDecoderGroup group = { size, word_length, channels };
    return sceAtracQueryDecoderGroupMemSize(type, &group);
}

static const SceAtracDecoderGroup two_channels = { sizeof(SceAtracDecoderGroup), SCE_ATRAC_WORD_LENGTH_16BITS, 2 };

static SceUChar8 *put_bytes(SceUChar8 *p, const void *bytes, int len) {
    sceClibMemcpy(p, bytes, len);
    return p + len;
}

static SceUChar8 *put32(SceUChar8 *p, SceUInt32 value) {
    for (int i = 0; i < 4; i++)
        *p++ = (SceUChar8)(value >> (i * 8));
    return p;
}

static SceUChar8 *put16(SceUChar8 *p, SceUInt32 value) {
    *p++ = (SceUChar8)value;
    *p++ = (SceUChar8)(value >> 8);
    return p;
}

// ATRAC9 at 48 kHz, stereo, 96 bytes a frame and 4 frames a superframe
static const SceUChar8 config[4] = { 0xFE, 0x72, 0x0B, 0xF0 };
#define BLOCK_ALIGN 384
#define DATA_SIZE (BLOCK_ALIGN * 8)

// Writes a WAVE file to file, with the given format tag, and the ATRAC9 extension when it is
// 0xFFFE, followed by DATA_SIZE zeros. Returns its size.
static int build_wave(const char *riff, const char *wave, SceUInt32 format, const SceUChar8 guid[16]) {
    sceClibMemset(file, 0, sizeof(file));
    SceUChar8 *p = put_bytes(file, riff, 4);
    SceUChar8 *riff_size = p;
    p = put32(p, 0);
    p = put_bytes(p, wave, 4);
    p = put_bytes(p, "fmt ", 4);
    p = put32(p, format == 0xFFFE ? 0x34 : 0x10);
    p = put16(p, format);
    p = put16(p, 2);
    p = put32(p, 48000);
    p = put32(p, 18000);
    p = put16(p, BLOCK_ALIGN);
    p = put16(p, format == 0xFFFE ? 0 : 16);
    if (format == 0xFFFE) {
        p = put16(p, 0x22);
        p = put16(p, 1024);
        p = put32(p, 3);
        p = put_bytes(p, guid, 16);
        p = put32(p, 1);
        p = put_bytes(p, config, 4);
        p = put32(p, 0);
        p = put_bytes(p, "fact", 4);
        p = put32(p, 12);
        p = put32(p, 8 * 1024 - 256);
        p = put32(p, 256);
        p = put32(p, 0);
    }
    p = put_bytes(p, "data", 4);
    p = put32(p, DATA_SIZE);
    p += DATA_SIZE;
    put32(riff_size, p - file - 8);
    return p - file;
}

static const SceUChar8 at9_guid[16] = { 0xD2, 0x42, 0xE1, 0x47, 0xBA, 0x36, 0x8D, 0x4D, 0x88, 0xFC, 0x61, 0x65, 0x4F, 0x8C, 0x83, 0x6C };
static const SceUChar8 other_guid[16] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };

// Takes a handle for file, read_size bytes of it read into a buffer of buffer_size rounded up to the
// alignment a buffer's size needs, prints the result, and releases it
static void acquire(const char *what, SceUInt32 read_size, SceUInt32 buffer_size) {
    const int handle = sceAtracSetDataAndAcquireHandle(file, read_size, SCE_ATRAC_ROUND_UP(buffer_size));
    PRINTF(handle < 0 ? handle : 0, "take a handle for %s", what);
    if (handle >= 0)
        sceAtracReleaseHandle(handle);
}

static void print_group_info(const char *what) {
    SceAtracDecoderGroup created, available;
    sceClibMemset(&created, 0x55, sizeof(created));
    sceClibMemset(&available, 0x55, sizeof(available));
    created.size = available.size = sizeof(SceAtracDecoderGroup);
    PRINTF(sceAtracGetDecoderGroupInfo(SCE_ATRAC_TYPE_AT9, &created, &available), "%s: get the group info", what);
    PRINTF(created.wordLength, "%s: created word length", what);
    PRINTF(created.totalCh, "%s: created channels", what);
    PRINTF(available.wordLength, "%s: available word length", what);
    PRINTF(available.totalCh, "%s: available channels", what);
}

int main(void) {
    PRINT("load the atrac module", sceSysmoduleLoadModule(SCE_SYSMODULE_ATRAC));

    // Memory for a decoder group
    static const SceUInt32 channels[] = { 0, 1, 2, 4, 8, 15, 16, 17 };
    for (unsigned int i = 0; i < sizeof(channels) / sizeof(channels[0]); i++)
        PRINTF(query(SCE_ATRAC_TYPE_AT9, sizeof(SceAtracDecoderGroup), 16, channels[i]), "group memory for %u channels", channels[i]);
    PRINT("group memory with 24 bit words", query(SCE_ATRAC_TYPE_AT9, sizeof(SceAtracDecoderGroup), 24, 2));
    PRINT("group memory with 0 bit words", query(SCE_ATRAC_TYPE_AT9, sizeof(SceAtracDecoderGroup), 0, 2));
    PRINT("group memory with a size of 8", query(SCE_ATRAC_TYPE_AT9, 8, 16, 2));
    PRINT("group memory with a size of 16", query(SCE_ATRAC_TYPE_AT9, 16, 16, 2));
    PRINT("group memory for type 0x2004", query(0x2004, sizeof(SceAtracDecoderGroup), 16, 2));
    PRINT("group memory for the audiodec type", query(SCE_AUDIODEC_TYPE_AT9, sizeof(SceAtracDecoderGroup), 16, 2));

    // Before a group is created
    print_group_info("before a group");
    PRINT("delete a group before creating one", sceAtracDeleteDecoderGroup(SCE_ATRAC_TYPE_AT9, 1));
    build_wave("RIFF", "WAVE", 0xFFFE, at9_guid);
    PRINT("take a handle before a group", sceAtracSetDataAndAcquireHandle(file, sizeof(file), sizeof(file)));

    // Creating one, which initializes audiodec's AT9 library when asked to
    const int need = query(SCE_ATRAC_TYPE_AT9, sizeof(SceAtracDecoderGroup), 16, 2);
    PRINT("the group fits in the work memory", need > 0 && need <= (int)sizeof(work) - 256);
    PRINT("create a group 4 bytes past alignment", sceAtracCreateDecoderGroup(SCE_ATRAC_TYPE_AT9, &two_channels, work + 4, 1));
    PRINT("create a group with init flag 2", sceAtracCreateDecoderGroup(SCE_ATRAC_TYPE_AT9, &two_channels, work, 2));
    PRINT("create a group", sceAtracCreateDecoderGroup(SCE_ATRAC_TYPE_AT9, &two_channels, work, 1));
    PRINT("create it again", sceAtracCreateDecoderGroup(SCE_ATRAC_TYPE_AT9, &two_channels, work, 1));
    PRINT("init audiodec's AT9 library after it", sceAudiodecInitLibrary(SCE_AUDIODEC_TYPE_AT9, &(SceAudiodecInitParam){ .at9 = { 8, 2 } }));
    print_group_info("with a group");

    // Files it doesn't take
    const int size = build_wave("RIFF", "WAVE", 0xFFFE, at9_guid);
    PRINT("the built file's size", size);
    sceClibMemset(file, 0, sizeof(file));
    acquire("zeros", size, size);
    build_wave("RIFX", "WAVE", 0xFFFE, at9_guid);
    acquire("a file that isn't RIFF", size, size);
    build_wave("RIFF", "AVI ", 0xFFFE, at9_guid);
    acquire("a RIFF file that isn't WAVE", size, size);
    const int pcm_size = build_wave("RIFF", "WAVE", 1, at9_guid);
    acquire("a PCM file", pcm_size, pcm_size);
    build_wave("RIFF", "WAVE", 0xFFFE, other_guid);
    acquire("an extensible file of another format", size, size);
    build_wave("RIFF", "WAVE", 0xFFFE, at9_guid);
    file[0x40] = 0xFF;
    acquire("an ATRAC9 file with a bad config sync byte", size, size);
    build_wave("RIFF", "WAVE", 0xFFFE, at9_guid);
    acquire("8 bytes of the ATRAC9 file", 8, size);
    acquire("the ATRAC9 file's header without data", 0x60, size);
    acquire("the ATRAC9 file read past its buffer", size, 0x100);
    acquire("the ATRAC9 file in a buffer of 0", 0, 0);

    // The ATRAC9 file, all of it in memory
    const int handle = sceAtracSetDataAndAcquireHandle(file, size, SCE_ATRAC_ROUND_UP(size));
    PRINT("take a handle for the ATRAC9 file", handle < 0 ? handle : 0);
    if (handle >= 0) {
        SceAtracContentInfo content;
        sceClibMemset(&content, 0x55, sizeof(content));
        content.size = sizeof(content);
        PRINT("get its content info", sceAtracGetContentInfo(handle, &content));
        PRINT("its type", content.atracType);
        PRINT("its channels", content.channel);
        PRINT("its sampling rate", content.samplingRate);
        PRINT("its end sample", content.endSample);
        PRINT("its loop start", content.loopStartSample);
        PRINT("its loop end", content.loopEndSample);
        PRINT("its bit rate", content.bitRate);
        PRINT("its block size", content.fixedEncBlockSize);
        PRINT("its block samples", content.fixedEncBlockSample);
        PRINT("its frame samples", content.frameSample);
        PRINT("its loop block offset", content.loopBlockOffset);
        PRINT("its loop block size", content.loopBlockSize);
        SceUInt32 value = 0x55555555;
        PRINT("get its output samples", sceAtracGetOutputSamples(handle, &value));
        PRINT("its output samples", value);
        value = 0x55555555;
        PRINT("get its next output position", sceAtracGetNextOutputPosition(handle, &value));
        PRINT("its next output position", value);
        SceLong64 remain = 0x55555555;
        PRINT("get its remaining samples", sceAtracGetRemainSamples(handle, &remain));
        PRINT("its remaining samples", (int)remain);
        value = 0x55555555;
        PRINT("get its decoder status", sceAtracGetDecoderStatus(handle, &value));
        PRINT("its decoder status", value);
        value = 0x55555555;
        PRINT("get its vacant size", sceAtracGetVacantSize(handle, &value));
        PRINT("its vacant size", value);
        PRINT("whether it needs a sub buffer", sceAtracIsSubBufferNeeded(handle));
        int loops = 0x55;
        SceUInt32 loop_status = 0x55;
        PRINT("get its loop info", sceAtracGetLoopInfo(handle, &loops, &loop_status));
        PRINT("its loops", loops);
        PRINT("its loop status", loop_status);
        PRINT("set 1 loop", sceAtracSetLoopNum(handle, 1));
        PRINT("set 0 loops", sceAtracSetLoopNum(handle, 0));
        PRINT("set 256 output samples", sceAtracSetOutputSamples(handle, 256));
        PRINT("set 100 output samples", sceAtracSetOutputSamples(handle, 100));
        PRINT("set 4096 output samples", sceAtracSetOutputSamples(handle, 4096));
        PRINT("set 1024 output samples", sceAtracSetOutputSamples(handle, 1024));
        SceUInt32 decoded = 0x55555555, status = 0x55555555;
        PRINT("decode its zeros", sceAtracDecode(handle, output, &decoded, &status));
        PRINT("the samples decoded", decoded);
        PRINT("the status after it", status);
        value = 0x55555555;
        sceAtracGetNextOutputPosition(handle, &value);
        PRINT("its next output position after it", value);
        PRINT("reset the output position to 0", sceAtracResetNextOutputPosition(handle, 0));
        PRINT("reset it past the end", sceAtracResetNextOutputPosition(handle, 0x100000));
        PRINT("add stream data to a file all in memory", sceAtracAddStreamData(handle, 16));
        int internal = 0x55;
        PRINT("get its internal error", sceAtracGetInternalError(handle, &internal));
        PRINT("its internal error", internal);
        const int second = sceAtracSetDataAndAcquireHandle(file, size, SCE_ATRAC_ROUND_UP(size));
        PRINT("take a second handle for the file", second < 0 ? second : 0);
        PRINT("take a third handle", sceAtracSetDataAndAcquireHandle(file, size, SCE_ATRAC_ROUND_UP(size)) < 0);
        PRINT("delete the group with handles left", sceAtracDeleteDecoderGroup(SCE_ATRAC_TYPE_AT9, 1));
        if (second >= 0)
            sceAtracReleaseHandle(second);
        PRINT("release the handle", sceAtracReleaseHandle(handle));
        PRINT("release it again", sceAtracReleaseHandle(handle));
        PRINT("get the content info of the released handle", sceAtracGetContentInfo(handle, &content));
    }
    SceUInt32 dummy = 0;
    PRINT("release handle -1", sceAtracReleaseHandle(-1));
    PRINT("release handle 0x12345", sceAtracReleaseHandle(0x12345));
    PRINT("decode with handle -1", sceAtracDecode(-1, output, &dummy, &dummy));

    // Deleting the group, which terms the audiodec library when asked to
    PRINT("delete the group with type 0x2004", sceAtracDeleteDecoderGroup(0x2004, 1));
    PRINT("delete the group", sceAtracDeleteDecoderGroup(SCE_ATRAC_TYPE_AT9, 1));
    PRINT("delete it again", sceAtracDeleteDecoderGroup(SCE_ATRAC_TYPE_AT9, 1));
    PRINT("term audiodec's AT9 library after it", sceAudiodecTermLibrary(SCE_AUDIODEC_TYPE_AT9));

    // A group without the init, on a library initialized by hand for fewer channels
    PRINT("init audiodec's AT9 library for 1 channel", sceAudiodecInitLibrary(SCE_AUDIODEC_TYPE_AT9, &(SceAudiodecInitParam){ .at9 = { 8, 1 } }));
    PRINT("create a group of 2 channels without init", sceAtracCreateDecoderGroup(SCE_ATRAC_TYPE_AT9, &two_channels, work, 0));
    PRINT("take a handle for the stereo file", sceAtracSetDataAndAcquireHandle(file, size, SCE_ATRAC_ROUND_UP(size)) < 0);
    PRINT("delete it without term", sceAtracDeleteDecoderGroup(SCE_ATRAC_TYPE_AT9, 0));
    PRINT("term audiodec's AT9 library", sceAudiodecTermLibrary(SCE_AUDIODEC_TYPE_AT9));

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("group memory for NULL", sceAtracQueryDecoderGroupMemSize(SCE_ATRAC_TYPE_AT9, NULL));
    PRINT("create a group in NULL memory", sceAtracCreateDecoderGroup(SCE_ATRAC_TYPE_AT9, &two_channels, NULL, 1));
    sceAtracCreateDecoderGroup(SCE_ATRAC_TYPE_AT9, &two_channels, work, 1);
    PRINT("take a handle for a NULL buffer", sceAtracSetDataAndAcquireHandle(NULL, size, SCE_ATRAC_ROUND_UP(size)));
    PRINT("get the group info into NULL", sceAtracGetDecoderGroupInfo(SCE_ATRAC_TYPE_AT9, NULL, NULL));
    sceAtracDeleteDecoderGroup(SCE_ATRAC_TYPE_AT9, 1);
    PRINT("unload the atrac module", sceSysmoduleUnloadModule(SCE_SYSMODULE_ATRAC));

    test_exit();
    return 0;
}
