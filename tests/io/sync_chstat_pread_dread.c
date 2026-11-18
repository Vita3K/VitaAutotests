// sceIoSyncByFd, sceIoChstat, sceIoPread, sceIoPwrite and sceIoDread, in the cases where their results are odd
#include <psp2/io/dirent.h>

#include "async_io.h"

#define DIR_PATH "ux0:data/vita-autotests/io_sync"
#define FILE_PATH DIR_PATH "/file"
#define SUB_PATH DIR_PATH "/sub"

static void clean(void) {
    sceIoRemove(FILE_PATH);
    sceIoRmdir(SUB_PATH);
    sceIoRmdir(DIR_PATH);
}

static void sync_and_chstat(void) {
    const SceUID reader = sceIoOpen(FILE_PATH, SCE_O_RDONLY, 0);
    const SceUID writer = sceIoOpen(FILE_PATH, SCE_O_WRONLY, 0);
    PRINT("sync a read only descriptor", sceIoSyncByFd(reader, 0));
    PRINT("sync a write descriptor", sceIoSyncByFd(writer, 0));
    PRINT("sync a write descriptor with a flag", sceIoSyncByFd(writer, 1));
    PRINT("sync a closed descriptor", sceIoSyncByFd(-1, 0));
    sceIoClose(reader);
    sceIoClose(writer);
    PRINT("sync a closed descriptor again", sceIoSyncByFd(reader, 0));

    SceIoStat stat;
    sceClibMemset(&stat, 0, sizeof(stat));
    stat.st_size = 3;
    PRINT("change the size of a directory", sceIoChstat(SUB_PATH, &stat, SCE_CST_SIZE));
    PRINT("change the size of a file", sceIoChstat(FILE_PATH, &stat, SCE_CST_SIZE));
    SceIoStat now;
    PRINT("stat the file", sceIoGetstat(FILE_PATH, &now));
    PRINT("its size", (int)now.st_size);

    sceClibMemset(&stat, 0, sizeof(stat));
    stat.st_mtime.year = 2020;
    stat.st_mtime.month = 13;
    stat.st_mtime.day = 1;
    PRINT("change the time to month 13", sceIoChstat(FILE_PATH, &stat, SCE_CST_MT));
    stat.st_mtime.month = 2;
    stat.st_mtime.day = 3;
    PRINT("change the time to a valid date", sceIoChstat(FILE_PATH, &stat, SCE_CST_MT));
    sceIoGetstat(FILE_PATH, &now);
    PRINTF(now.st_mtime.year == 2020 && now.st_mtime.month == 2 && now.st_mtime.day == 3, "the modification date is the new one");

    sceIoGetstat(FILE_PATH, &now);
    PRINT("mode before", now.st_mode);
    PRINT("attributes before", now.st_attr);
    sceClibMemset(&stat, 0, sizeof(stat));
    stat.st_mode = 0;
    PRINT("change the mode to 0", sceIoChstat(FILE_PATH, &stat, SCE_CST_MODE));
    sceIoGetstat(FILE_PATH, &now);
    PRINT("mode after", now.st_mode);
    PRINT("attributes after", now.st_attr);
    stat.st_mode = SCE_S_IFREG | 0777;
    PRINT("change the mode back", sceIoChstat(FILE_PATH, &stat, SCE_CST_MODE));
    sceIoGetstat(FILE_PATH, &now);
    PRINT("mode at the end", now.st_mode);
    PRINT("attributes at the end", now.st_attr);
    PRINT("change a missing file", sceIoChstat(DIR_PATH "/missing", &stat, SCE_CST_MODE));
}

static void pread_edges(void) {
    // The file has 16 bytes
    async_make_file(FILE_PATH, "0123456789abcdef", 16);
    SceUID fd = sceIoOpen(FILE_PATH, SCE_O_RDONLY, 0);
    char buf[8] = { 0 };
    PRINT("pread at 4", sceIoPread(fd, buf, 4, 4));
    PRINT("it read the right bytes", sceClibStrncmp(buf, "4567", 4) == 0);
    PRINT("the position after the pread", (int)sceIoLseek(fd, 0, SCE_SEEK_CUR));
    PRINT("pread at a negative offset", sceIoPread(fd, buf, 4, -1));
    PRINT("pread at the size", sceIoPread(fd, buf, 4, 16));
    PRINT("pread past the size", sceIoPread(fd, buf, 4, 100));
    PRINT("pread across the end", sceIoPread(fd, buf, 8, 12));
    PRINT("pread of 0 bytes", sceIoPread(fd, buf, 0, 0));
    PRINT("pread into a null buffer", sceIoPread(fd, NULL, 4, 0));
    PRINT("pread into a null buffer at the size", sceIoPread(fd, NULL, 4, 16));
    PRINT("pwrite on a read only descriptor", sceIoPwrite(fd, "ab", 2, 0));
    sceIoClose(fd);

    fd = sceIoOpen(FILE_PATH, SCE_O_WRONLY, 0);
    PRINT("pread on a write only descriptor", sceIoPread(fd, buf, 4, 0));
    sceIoClose(fd);

    const SceUID dir = sceIoDopen(DIR_PATH);
    PRINT("pread on a directory", sceIoPread(dir, buf, 4, 0));
    PRINT("pwrite on a directory", sceIoPwrite(dir, buf, 4, 0));
    sceIoDclose(dir);

    fd = sceIoOpen(FILE_PATH, SCE_O_WRONLY | SCE_O_APPEND, 0);
    PRINT("pwrite at 0 on an append descriptor", sceIoPwrite(fd, "XY", 2, 0));
    PRINT("the position after the pwrite", (int)sceIoLseek(fd, 0, SCE_SEEK_CUR));
    PRINT("pwrite at a negative offset", sceIoPwrite(fd, "XY", 2, -1));
    sceIoClose(fd);
    SceIoStat stat;
    sceIoGetstat(FILE_PATH, &stat);
    PRINT("the file size after", (int)stat.st_size);
    char all[20] = { 0 };
    fd = sceIoOpen(FILE_PATH, SCE_O_RDONLY, 0);
    PRINT("read the file", sceIoRead(fd, all, 19));
    sceIoClose(fd);
    PRINT("the bytes at offset 0 are the pwrite", sceClibStrncmp(all, "XY23", 4) == 0);
    PRINT("the bytes at the end are the pwrite", all[16] == 'X' && all[17] == 'Y');
}

static void dread_fields(void) {
    const SceUID dir = sceIoDopen(DIR_PATH);
    int count = 0, attr_nonzero = 0, after_name_nonzero = 0, private_changed = 0, dummy_changed = 0;
    int ret;
    SceIoDirent entry;
    while (1) {
        sceClibMemset(&entry, 0xAB, sizeof(entry));
        ret = sceIoDread(dir, &entry);
        if (ret <= 0)
            break;
        count++;
        attr_nonzero += entry.d_stat.st_attr != 0;
        after_name_nonzero += entry.d_name[sceClibStrnlen(entry.d_name, 256) + 1] != 0;
        private_changed += entry.d_private != (void *)0xABABABAB;
        dummy_changed += entry.dummy != (int)0xABABABAB;
    }
    PRINT("the entries", count);
    PRINT("the last result", ret);
    PRINT("entries with a nonzero attribute", attr_nonzero);
    PRINT("entries with a nonzero byte after the name", after_name_nonzero);
    PRINT("entries that changed d_private", private_changed);
    PRINT("entries that changed the last word", dummy_changed);
    // The end of the listing still copies the buffer back
    PRINT("the bytes after the name at the end", entry.d_name[10]);
    sceIoDclose(dir);
}

int main(void) {
    clean();
    sceIoMkdir(DIR_PATH, 0777);
    sceIoMkdir(SUB_PATH, 0777);
    async_make_file(FILE_PATH, "0123456789abcdef", 16);

    sync_and_chstat();
    pread_edges();
    dread_fields();

    clean();
    test_exit();
    return 0;
}
