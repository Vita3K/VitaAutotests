// Loading PAF, and the PAF calls that the vitasdk headers lack. A test that uses PAF starts with
// paf_load_or_skip().
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <psp2/paf.h>
#include <psp2/sysmodule.h>

#include "test.h"

// What libpaf takes as the argument of its load. The names of the fields after the heap size are
// guesses, the values are what homebrew that uses PAF passes.
typedef struct PafInit {
    SceSize global_heap_size;
    int a2;
    int a3;
    int use_gxm;
    int heap_opt_param1;
    int heap_opt_param2;
} PafInit;

#define PAF_INIT_DEFAULT { 0x1000000, 0xEA60, 0x40000, 0, 0, 0 }

// PAF's wide characters are 16 bits, like the strings of its resources
typedef uint16_t paf_wchar;

// Loads libpaf with the arguments homebrew passes, and returns the result of the load
static inline int paf_load(void) {
    PafInit init = PAF_INIT_DEFAULT;
    int result = 0;
    const SceSysmoduleOpt opt = { .flags = 0, .result = &result };
    return sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(init), &init, &opt);
}

// Loads libpaf for a test that needs it, or ends the test as skipped when it doesn't load
static inline void paf_load_or_skip(void) {
    if (paf_load() < 0)
        test_skip("libpaf doesn't load");
}

// ScePafStdc, which the vitasdk header only has part of
int sce_paf_abs(int value);
double sce_paf_atof(const char *s);
int sce_paf_atoi(const char *s);
long sce_paf_atol(const char *s);
long long sce_paf_atoll(const char *s);
void *sce_paf_bsearch(const void *key, const void *base, SceSize count, SceSize size, int (*compare)(const void *, const void *));
void sce_paf_qsort(void *base, SceSize count, SceSize size, int (*compare)(const void *, const void *));
int sce_paf_rand(void);
void sce_paf_srand(unsigned int seed);
int sce_paf_tolower(int ch);
int sce_paf_toupper(int ch);
int sce_paf_look_ctype_table(int ch);
char *sce_paf_strcat(char *dst, const char *src);
char *sce_paf_strncat(char *dst, const char *src, SceSize len);
char *sce_paf_strcpy(char *dst, const char *src);
SceSize sce_paf_strlcat(char *dst, const char *src, SceSize size);
SceSize sce_paf_strlcpy(char *dst, const char *src, SceSize size);
SceSize sce_paf_strcspn(const char *s, const char *reject);
SceSize sce_paf_strspn(const char *s, const char *accept);
char *sce_paf_strpbrk(const char *s, const char *accept);
char *sce_paf_strtok_r(char *s, const char *delim, char **save);
char *sce_paf_private_strtok(char *s, const char *delim);
long sce_paf_strtol(const char *s, char **end, int base);
long long sce_paf_strtoll(const char *s, char **end, int base);
unsigned long sce_paf_strtoul(const char *s, char **end, int base);
unsigned long long sce_paf_strtoull(const char *s, char **end, int base);
int sce_paf_sprintf(char *dst, const char *fmt, ...);
int sce_paf_vsprintf(char *dst, const char *fmt, va_list args);
int sce_paf_swprintf(paf_wchar *dst, SceSize max, const paf_wchar *fmt, ...);

int sce_paf_wcscasecmp(const paf_wchar *a, const paf_wchar *b);
paf_wchar *sce_paf_wcscat(paf_wchar *dst, const paf_wchar *src);
paf_wchar *sce_paf_wcschr(const paf_wchar *s, paf_wchar ch);
int sce_paf_wcscmp(const paf_wchar *a, const paf_wchar *b);
paf_wchar *sce_paf_wcscpy(paf_wchar *dst, const paf_wchar *src);
SceSize sce_paf_wcscspn(const paf_wchar *s, const paf_wchar *reject);
SceSize sce_paf_wcslen(const paf_wchar *s);
int sce_paf_wcsncasecmp(const paf_wchar *a, const paf_wchar *b, SceSize len);
paf_wchar *sce_paf_wcsncat(paf_wchar *dst, const paf_wchar *src, SceSize len);
int sce_paf_wcsncmp(const paf_wchar *a, const paf_wchar *b, SceSize len);
paf_wchar *sce_paf_wcsncpy(paf_wchar *dst, const paf_wchar *src, SceSize len);
SceSize sce_paf_wcsnlen(const paf_wchar *s, SceSize max);
paf_wchar *sce_paf_wcspbrk(const paf_wchar *s, const paf_wchar *accept);
paf_wchar *sce_paf_wcsrchr(const paf_wchar *s, paf_wchar ch);
SceSize sce_paf_wcsspn(const paf_wchar *s, const paf_wchar *accept);
paf_wchar *sce_paf_wmemchr(const paf_wchar *s, paf_wchar ch, SceSize count);
int sce_paf_wmemcmp(const paf_wchar *a, const paf_wchar *b, SceSize count);
paf_wchar *sce_paf_wmemcpy(paf_wchar *dst, const paf_wchar *src, SceSize count);
paf_wchar *sce_paf_wmemmove(paf_wchar *dst, const paf_wchar *src, SceSize count);
paf_wchar *sce_paf_wmemset(paf_wchar *dst, paf_wchar ch, SceSize count);
int sce_paf_private_towlower(int ch);

void *sce_paf_fopen(const char *path, const char *mode);
int sce_paf_fclose(void *file);
int sce_paf_fgetc(void *file);
int sce_paf_fputc(int ch, void *file);
SceSize sce_paf_fread(void *dst, SceSize size, SceSize count, void *file);
SceSize sce_paf_fwrite(const void *src, SceSize size, SceSize count, void *file);
int sce_paf_fseek(void *file, long offset, int whence);
long sce_paf_ftell(void *file);
int sce_paf_misc_does_file_exist(const char *path);

int sce_paf_setjmp(int *env);
void sce_paf_longjmp(int *env, int value);
