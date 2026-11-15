// How long the file calls a game makes while it loads take: opening a file the first time and again,
// sceIoGetstat on a file and on a missing one, opening a missing file, reading a whole small file,
// reading a big file through the first time and again, seeking and reading in another big one, and
// closing. Each is timed on app0:, with the files the vpk holds, and on ux0:, with copies the test
// writes first.
//
// A time is the median of FILE_COUNT calls, one per file, or of the reads or passes the case names.
// It is printed as whether it is at least about a tenth of what the call takes on a Vita, so the
// output stays the same from run to run and from Vita to Vita. The exact medians, with the fastest
// and slowest call, go to the debug output and to TIMES_PATH, which the runner doesn't compare.
//
// Some games depend on these calls taking time. Silent Hill: Book of Memories reads a 20 MiB level
// package through on its main thread while a loader thread it just started loads its own files and
// then hides the loaded packages from lookups for a while. The main thread's next lookup only finds
// its file when that read took long enough for the loader to be done.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>

#include "test.h"

#define FILE_COUNT 32
#define SMALL_SIZE (16 * 1024)
#define BIG_SIZE (1024 * 1024)
#define CHUNK_SIZE (64 * 1024)
#define SEEK_READ_SIZE (4 * 1024)
#define SEEK_READS 32
#define AGAIN_PASSES 4

#define APP0_DIR "app0:io_timing"
#define UX0_DIR "ux0:data/vita-autotests/io_timing"
#define TIMES_PATH TEST_OUTPUT_DIR "/io_timing_times.txt"

static char buf[CHUNK_SIZE];
static SceUID times_fd = -1;

static SceUInt32 now(void) {
    return (SceUInt32)sceKernelGetProcessTimeWide();
}

static SceUInt32 median(SceUInt32 *values, int count) {
    for (int i = 1; i < count; i++) {
        const SceUInt32 v = values[i];
        int j = i;
        for (; j > 0 && values[j - 1] > v; j--)
            values[j] = values[j - 1];
        values[j] = v;
    }
    return values[count / 2];
}

// Prints whether the median of the times is at least min_us, and writes it exactly with the fastest
// and slowest
static void report(const char *dev, const char *what, SceUInt32 *times, int count, SceUInt32 min_us) {
    SceUInt32 lo = times[0], hi = times[0];
    for (int i = 1; i < count; i++) {
        lo = times[i] < lo ? times[i] : lo;
        hi = times[i] > hi ? times[i] : hi;
    }
    const SceUInt32 mid = median(times, count);
    PRINTF(mid >= min_us, "%s: %s, median at least %u us", dev, what, (unsigned)min_us);
    char line[192];
    const int len = sceClibSnprintf(line, sizeof(line), "%s: %s: median %u us, fastest %u, slowest %u, of %d\n",
                                    dev, what, (unsigned)mid, (unsigned)lo, (unsigned)hi, count);
    sceClibPrintf("%s", line);
    if (times_fd >= 0)
        sceIoWrite(times_fd, line, len < (int)sizeof(line) ? len : (int)sizeof(line) - 1);
}

static void small_path(char *out, int size, const char *dir, const char *name, int i) {
    sceClibSnprintf(out, size, "%s/%s%d.bin", dir, name, i);
}

// Reads the open file through from the start in CHUNK_SIZE reads. Returns how many reads fell short.
static int read_through(SceUID fd) {
    int short_reads = 0;
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    for (int done = 0; done < BIG_SIZE; done += CHUNK_SIZE)
        short_reads += sceIoRead(fd, buf, CHUNK_SIZE) != CHUNK_SIZE;
    return short_reads;
}

static void time_device(const char *dev, const char *dir) {
    static SceUInt32 first[FILE_COUNT], again[FILE_COUNT], stat_file[FILE_COUNT], stat_missing[FILE_COUNT],
        open_missing[FILE_COUNT], read_small[FILE_COUNT], close_file[FILE_COUNT];
    static SceUInt32 big_first[1], big_again[AGAIN_PASSES], seek_read[SEEK_READS];
    char path[96];
    int failures = 0;

    for (int i = 0; i < FILE_COUNT; i++) {
        small_path(path, sizeof(path), dir, "f", i);
        SceUInt32 t = now();
        SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
        first[i] = now() - t;
        if (fd < 0) {
            failures++;
            continue;
        }
        t = now();
        failures += sceIoRead(fd, buf, SMALL_SIZE) != SMALL_SIZE;
        read_small[i] = now() - t;
        t = now();
        sceIoClose(fd);
        close_file[i] = now() - t;

        t = now();
        fd = sceIoOpen(path, SCE_O_RDONLY, 0);
        again[i] = now() - t;
        failures += fd < 0;
        if (fd >= 0)
            sceIoClose(fd);

        SceIoStat stat;
        t = now();
        failures += sceIoGetstat(path, &stat) < 0;
        stat_file[i] = now() - t;

        small_path(path, sizeof(path), dir, "missing", i);
        t = now();
        failures += sceIoGetstat(path, &stat) >= 0;
        stat_missing[i] = now() - t;
        t = now();
        fd = sceIoOpen(path, SCE_O_RDONLY, 0);
        open_missing[i] = now() - t;
        failures += fd >= 0;
        if (fd >= 0)
            sceIoClose(fd);
    }

    sceClibSnprintf(path, sizeof(path), "%s/big.bin", dir);
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    failures += fd < 0;
    for (int pass = 0; fd >= 0 && pass <= AGAIN_PASSES; pass++) {
        const SceUInt32 t = now();
        failures += read_through(fd);
        const SceUInt32 took = now() - t;
        if (pass == 0)
            big_first[0] = took;
        else
            big_again[pass - 1] = took;
    }
    if (fd >= 0)
        sceIoClose(fd);

    sceClibSnprintf(path, sizeof(path), "%s/package.bin", dir);
    fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    failures += fd < 0;
    for (int i = 0; fd >= 0 && i < SEEK_READS; i++) {
        // Spread over the file, out of order, the way a game reads the entries of a package
        const int offset = ((i * 13) % SEEK_READS) * (BIG_SIZE / SEEK_READS);
        const SceUInt32 t = now();
        sceIoLseek(fd, offset, SCE_SEEK_SET);
        failures += sceIoRead(fd, buf, SEEK_READ_SIZE) != SEEK_READ_SIZE;
        seek_read[i] = now() - t;
    }
    if (fd >= 0)
        sceIoClose(fd);

    PRINTF(failures, "%s: calls that didn't do what they should", dev);
    report(dev, "open a file the first time", first, FILE_COUNT, 100);
    report(dev, "open it again", again, FILE_COUNT, 100);
    report(dev, "getstat a file", stat_file, FILE_COUNT, 50);
    report(dev, "getstat a missing file", stat_missing, FILE_COUNT, 250);
    report(dev, "open a missing file", open_missing, FILE_COUNT, 50);
    report(dev, "read a whole 16 KiB file", read_small, FILE_COUNT, 1000);
    report(dev, "close a file", close_file, FILE_COUNT, 20);
    report(dev, "read a 1 MiB file through in 64 KiB reads, the first time", big_first, 1, 10000);
    report(dev, "read it through again", big_again, AGAIN_PASSES, 10000);
    report(dev, "seek and read 4 KiB in another 1 MiB file", seek_read, SEEK_READS, 250);
}

static int copy_file(const char *from, const char *to) {
    const SceUID in = sceIoOpen(from, SCE_O_RDONLY, 0);
    const SceUID out = sceIoOpen(to, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    int failures = in < 0 || out < 0;
    int n;
    while (in >= 0 && out >= 0 && (n = sceIoRead(in, buf, sizeof(buf))) > 0)
        failures += sceIoWrite(out, buf, n) != n;
    if (in >= 0)
        sceIoClose(in);
    if (out >= 0)
        sceIoClose(out);
    return failures;
}

// Copies the vpk's files to ux0:, so both devices are timed with the same files
static int copy_to_ux0(void) {
    char from[96], to[96];
    int failures = 0;
    sceIoMkdir(UX0_DIR, 0777);
    for (int i = 0; i < FILE_COUNT; i++) {
        small_path(from, sizeof(from), APP0_DIR, "f", i);
        small_path(to, sizeof(to), UX0_DIR, "f", i);
        failures += copy_file(from, to);
    }
    failures += copy_file(APP0_DIR "/big.bin", UX0_DIR "/big.bin");
    failures += copy_file(APP0_DIR "/package.bin", UX0_DIR "/package.bin");
    return failures;
}

int main(void) {
    times_fd = sceIoOpen(TIMES_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    // app0: first, before the test has read any of its files
    time_device("app0", APP0_DIR);
    PRINT("copy the files to ux0", copy_to_ux0());
    time_device("ux0", UX0_DIR);
    if (times_fd >= 0)
        sceIoClose(times_fd);
    test_exit();
    return 0;
}
