// The ScePafStdc file calls and ScePafMisc's file check: whether files exist, writing, reading back,
// seeking, reading a character at a time, and what opening a missing file gives
#include <psp2/io/stat.h>

#include "paf.h"

#define PATH "ux0:data/vita-autotests/paf_file.bin"

int main(void) {
    paf_load_or_skip();
    sceIoMkdir("ux0:data/vita-autotests", 0777);
    sceIoRemove(PATH);

    PRINT("a missing file exists", sce_paf_misc_does_file_exist(PATH));
    PRINT("a directory exists", sce_paf_misc_does_file_exist("ux0:data"));
    PRINT("an empty path exists", sce_paf_misc_does_file_exist(""));
    PRINT("a missing file opens", sce_paf_fopen(PATH, "rb") != NULL);

    void *file = sce_paf_fopen(PATH, "wb");
    PRINT("create the file", file != NULL);
    PRINT("write 8 bytes", sce_paf_fwrite("01234567", 1, 8, file));
    PRINT("write 2 items of 4", sce_paf_fwrite("abcdefgh", 4, 2, file));
    PRINT("put a character", sce_paf_fputc('Z', file));
    PRINT("the position", sce_paf_ftell(file));
    PRINT("close", sce_paf_fclose(file));
    PRINT("the file exists", sce_paf_misc_does_file_exist(PATH));

    file = sce_paf_fopen(PATH, "rb");
    PRINT("open the file", file != NULL);
    char buf[32];
    sce_paf_memset(buf, 'x', sizeof(buf));
    PRINT("read 4 bytes", sce_paf_fread(buf, 1, 4, file));
    PRINT("it reads them", sce_paf_memcmp(buf, "0123", 4) == 0 && buf[4] == 'x');
    PRINT("the position", sce_paf_ftell(file));
    PRINT("get a character", sce_paf_fgetc(file));
    PRINT("seek to 12 from the start", sce_paf_fseek(file, 12, 0));
    PRINT("the position", sce_paf_ftell(file));
    PRINT("get a character there", sce_paf_fgetc(file));
    PRINT("seek back 3 from here", sce_paf_fseek(file, -3, 1));
    PRINT("the position", sce_paf_ftell(file));
    PRINT("seek to 2 before the end", sce_paf_fseek(file, -2, 2));
    PRINT("the position", sce_paf_ftell(file));
    PRINT("read 8 items of 1 at the end", sce_paf_fread(buf, 1, 8, file));
    PRINT("get a character at the end", sce_paf_fgetc(file));
    PRINT("seek past the end", sce_paf_fseek(file, 100, 0));
    PRINT("the position", sce_paf_ftell(file));
    PRINT("seek before the start", sce_paf_fseek(file, -1, 0));
    PRINT("seek with a bad origin", sce_paf_fseek(file, 0, 9));
    sce_paf_fseek(file, 0, 0);
    PRINT("read 3 items of 4", sce_paf_fread(buf, 4, 3, file));
    PRINT("read 0 items", sce_paf_fread(buf, 4, 0, file));
    PRINT("read items of size 0", sce_paf_fread(buf, 0, 4, file));
    PRINT("write to a file opened for reading", sce_paf_fwrite("a", 1, 1, file));
    PRINT("close", sce_paf_fclose(file));

    // Appending, and read and write
    file = sce_paf_fopen(PATH, "ab");
    PRINT("append 2 bytes", sce_paf_fwrite("!!", 1, 2, file));
    sce_paf_fclose(file);
    file = sce_paf_fopen(PATH, "rb");
    sce_paf_fseek(file, 0, 2);
    PRINT("the size after appending", sce_paf_ftell(file));
    sce_paf_fclose(file);
    file = sce_paf_fopen(PATH, "r+b");
    PRINT("open for reading and writing", file != NULL);
    PRINT("write at the start", sce_paf_fwrite("AB", 1, 2, file));
    sce_paf_fseek(file, 0, 0);
    PRINT("read it back", sce_paf_fgetc(file));
    sce_paf_fclose(file);

    // Text mode and bad modes
    file = sce_paf_fopen(PATH, "r");
    PRINT("open in text mode", file != NULL);
    sce_paf_fclose(file);
    PRINT("open with a bad mode", sce_paf_fopen(PATH, "q") != NULL);

    sceIoRemove(PATH);
    PRINT("the file exists after removing it", sce_paf_misc_does_file_exist(PATH));

    // NULL goes last
    PRINT("open a NULL path", sce_paf_fopen(NULL, "rb") != NULL);
    PRINT("close NULL", sce_paf_fclose(NULL));

    test_exit();
    return 0;
}
