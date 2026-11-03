// Writes captures as PNG files. The pixels are compressed with fixed Huffman codes and runs of the
// byte before, which is little code and shrinks the flat areas render tests draw to almost nothing.
// Each row is filtered with Up when it's the same as the row before, which makes it zeros, or Sub,
// which makes a run of one color zeros.
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include "capture.h"
#include "test.h"

#define MAX_WIDTH 1024
#define CHUNK_SIZE 0x8000
// The longest match deflate has
#define MAX_RUN 258
// How many bytes Adler-32 can add up before its sums must be reduced
#define ADLER_NMAX 5552

static struct {
    SceUID fd;
    int error;
    // The zlib stream, written out as an IDAT chunk each time it fills
    unsigned char chunk[CHUNK_SIZE];
    int chunk_len;
    unsigned int bits;
    int bit_count;
    // The byte before, and how many times it came again since it was written
    int last;
    int run;
    unsigned int adler_a, adler_b;
    int adler_pending;
} png;

static unsigned char rows[2][MAX_WIDTH * 4];
static unsigned char filtered[1 + MAX_WIDTH * 4];
static unsigned int crc_table[256];

static void write_out(const void *data, int len) {
    if (png.error < 0 || len == 0)
        return;
    const int written = sceIoWrite(png.fd, data, len);
    if (written < 0)
        png.error = written;
    else if (written != len)
        png.error = -1;
}

static unsigned int crc_update(unsigned int crc, const unsigned char *data, int len) {
    if (crc_table[1] == 0) {
        for (unsigned int n = 0; n < 256; n++) {
            unsigned int c = n;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[n] = c;
        }
    }
    for (int i = 0; i < len; i++)
        crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put_be32(unsigned char *p, unsigned int value) {
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

static void write_chunk(const char *type, const unsigned char *data, int len) {
    unsigned char header[8];
    put_be32(header, len);
    sceClibMemcpy(header + 4, type, 4);
    unsigned int crc = crc_update(0xFFFFFFFFu, header + 4, 4);
    crc = crc_update(crc, data, len);
    unsigned char footer[4];
    put_be32(footer, ~crc);
    write_out(header, 8);
    write_out(data, len);
    write_out(footer, 4);
}

static void flush_chunk(void) {
    if (png.chunk_len > 0)
        write_chunk("IDAT", png.chunk, png.chunk_len);
    png.chunk_len = 0;
}

static void put_byte(unsigned char byte) {
    png.chunk[png.chunk_len++] = byte;
    if (png.chunk_len == CHUNK_SIZE)
        flush_chunk();
}

// Writes the low count bits of value, the first bit first, as deflate packs them
static void put_bits(unsigned int value, int count) {
    png.bits |= value << png.bit_count;
    png.bit_count += count;
    while (png.bit_count >= 8) {
        put_byte(png.bits & 0xFF);
        png.bits >>= 8;
        png.bit_count -= 8;
    }
}

// Writes a Huffman code, which deflate packs from its highest bit
static void put_code(unsigned int code, int length) {
    unsigned int reversed = 0;
    for (int i = 0; i < length; i++)
        reversed |= ((code >> i) & 1) << (length - 1 - i);
    put_bits(reversed, length);
}

// Writes a literal, a length or the end of the block, with the fixed Huffman codes
static void put_symbol(int symbol) {
    if (symbol < 144)
        put_code(0x30 + symbol, 8);
    else if (symbol < 256)
        put_code(0x190 + symbol - 144, 9);
    else if (symbol < 280)
        put_code(symbol - 256, 7);
    else
        put_code(0xC0 + symbol - 280, 8);
}

static const unsigned short length_base[29] = { 3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char length_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };

// Writes the bytes of the run so far, as a match a byte back when there are enough of them
static void flush_run(void) {
    if (png.run >= 3) {
        int code = 28;
        while (length_base[code] > png.run)
            code--;
        put_symbol(257 + code);
        put_bits(png.run - length_base[code], length_extra[code]);
        // Distance code 0, a byte back
        put_code(0, 5);
    } else {
        for (int i = 0; i < png.run; i++)
            put_symbol(png.last);
    }
    png.run = 0;
}

static void adler_reduce(void) {
    png.adler_a %= 65521;
    png.adler_b %= 65521;
    png.adler_pending = 0;
}

static void compress(const unsigned char *data, int len) {
    for (int i = 0; i < len; i++) {
        const int byte = data[i];
        png.adler_a += byte;
        png.adler_b += png.adler_a;
        if (++png.adler_pending == ADLER_NMAX)
            adler_reduce();
        if (byte == png.last) {
            if (++png.run == MAX_RUN)
                flush_run();
        } else {
            flush_run();
            put_symbol(byte);
            png.last = byte;
        }
    }
}

static int capture_path(char *path, int size, const char *name) {
    sceIoMkdir(TEST_OUTPUT_DIR, 0777);
    sceClibSnprintf(path, size, TEST_OUTPUT_DIR "/%s", test_name);
    sceIoMkdir(path, 0777);
    return sceClibSnprintf(path, size, TEST_OUTPUT_DIR "/%s/%s.png", test_name, name);
}

static int write_png(const char *name, const unsigned char *pixels, int width, int height, int stride) {
    if (width <= 0 || width > MAX_WIDTH || height <= 0 || stride < width)
        return -1;
    char path[160];
    capture_path(path, sizeof(path), name);
    png.fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (png.fd < 0)
        return png.fd;
    png.error = 0;
    png.chunk_len = 0;
    png.bits = 0;
    png.bit_count = 0;
    // No byte before the first one
    png.last = -1;
    png.run = 0;
    png.adler_a = 1;
    png.adler_b = 0;
    png.adler_pending = 0;

    static const unsigned char signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    write_out(signature, sizeof(signature));
    unsigned char header[13];
    put_be32(header, width);
    put_be32(header + 4, height);
    header[8] = 8;  // bits per channel
    header[9] = 6;  // RGBA, which A8B8G8R8 already is in memory
    header[10] = 0; // deflate
    header[11] = 0; // filters by row
    header[12] = 0; // not interlaced
    write_chunk("IHDR", header, sizeof(header));

    // The zlib header, and a single block with the fixed codes
    put_byte(0x78);
    put_byte(0x01);
    put_bits(1, 1);
    put_bits(1, 2);
    const int row_size = width * 4;
    for (int y = 0; y < height; y++) {
        unsigned char *row = rows[y & 1];
        const unsigned char *above = rows[(y & 1) ^ 1];
        // Copied first, since the framebuffer is in uncached memory
        sceClibMemcpy(row, pixels + (y * stride) * 4, row_size);
        if (y > 0 && sceClibMemcmp(row, above, row_size) == 0) {
            filtered[0] = 2;
            sceClibMemset(filtered + 1, 0, row_size);
        } else {
            filtered[0] = 1;
            for (int i = 0; i < row_size; i++)
                filtered[1 + i] = row[i] - (i >= 4 ? row[i - 4] : 0);
        }
        compress(filtered, 1 + row_size);
    }
    flush_run();
    put_symbol(256);
    // The bits left over, padded to a byte, then the Adler-32 of the data
    if (png.bit_count > 0)
        put_bits(0, 8 - png.bit_count);
    adler_reduce();
    const unsigned int adler = (png.adler_b << 16) | png.adler_a;
    put_byte(adler >> 24);
    put_byte(adler >> 16);
    put_byte(adler >> 8);
    put_byte(adler);
    flush_chunk();
    write_chunk("IEND", NULL, 0);

    const int closed = sceIoClose(png.fd);
    if (png.error < 0)
        return png.error;
    return closed < 0 ? closed : 0;
}

int test_capture(const char *name, const void *pixels, int width, int height, int stride) {
    const int result = write_png(name, pixels, width, height, stride);
    PRINTF(result, "capture %s", name);
    return result;
}

int test_capture_display(const char *name) {
    SceDisplayFrameBuf fb = { .size = sizeof(fb) };
    int result = sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    if (result >= 0 && (fb.base == NULL || fb.pixelformat != SCE_DISPLAY_PIXELFORMAT_A8B8G8R8))
        result = -1;
    if (result >= 0)
        result = write_png(name, fb.base, fb.width, fb.height, fb.pitch);
    PRINTF(result, "capture %s", name);
    return result;
}
