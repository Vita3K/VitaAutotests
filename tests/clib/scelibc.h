// SceLibc and SceLibm calls, under the names scelibc_stub.cmake gives their stubs, so they don't clash
// with newlib's. A Vita links them when it starts the process, since the app imports them.
#pragma once

#include <stddef.h>

typedef void (*scelibc_constraint_handler)(const char *msg, void *ptr, int error);

void scelibc_qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
int scelibc_qsort_s(void *base, size_t count, size_t size, int (*compare)(const void *, const void *, void *), void *context);
void *scelibc_bsearch(const void *key, const void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
void *scelibc_bsearch_s(const void *key, const void *base, size_t count, size_t size,
                        int (*compare)(const void *, const void *, void *), void *context);
scelibc_constraint_handler scelibc_set_constraint_handler_s(scelibc_constraint_handler handler);
void scelibc_ignore_handler_s(const char *msg, void *ptr, int error);
int *scelibc__sceLibcErrnoLoc(void);

double scelibc_sin(double x);
double scelibc_cos(double x);
double scelibc_tan(double x);
double scelibc_asin(double x);
double scelibc_acos(double x);
double scelibc_atan(double x);
double scelibc_atan2(double y, double x);
double scelibc_exp(double x);
double scelibc_log(double x);
double scelibc_log10(double x);
double scelibc_pow(double x, double y);
double scelibc_sqrt(double x);
double scelibc_fmod(double x, double y);
double scelibc_remainder(double x, double y);
double scelibc_floor(double x);
double scelibc_ceil(double x);
double scelibc_round(double x);
double scelibc_trunc(double x);
double scelibc_rint(double x);
long scelibc_lround(double x);
long scelibc_lrint(double x);
double scelibc_modf(double x, double *integer);
double scelibc_frexp(double x, int *exponent);
double scelibc_ldexp(double x, int exponent);
double scelibc_hypot(double x, double y);
double scelibc_cbrt(double x);
double scelibc_fma(double x, double y, double z);
double scelibc_nextafter(double x, double y);
double scelibc_copysign(double x, double y);
float scelibc_sinf(float x);
float scelibc_cosf(float x);
float scelibc_sqrtf(float x);
float scelibc_expf(float x);
float scelibc_logf(float x);
float scelibc_powf(float x, float y);
float scelibc_fmodf(float x, float y);
float scelibc_atan2f(float y, float x);
