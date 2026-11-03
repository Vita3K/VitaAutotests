// SceLibm: the exact bits of what its math calls return for ordinary
// and special values (zeros, infinities, NaNs, denormals and large arguments), how they round, and
// the errno they leave on domain and range errors. A double prints as 16 hex digits.
#include "scelibc.h"
#include "test.h"

typedef union {
    double d;
    unsigned long long u;
} double_bits;

typedef union {
    float f;
    unsigned int u;
} float_bits;

static double from_bits(unsigned long long u) {
    const double_bits b = { .u = u };
    return b.d;
}

static float float_from_bits(unsigned int u) {
    const float_bits b = { .u = u };
    return b.f;
}

// Prints a double's bits, under a label built from fmt like printf
__attribute__((format(printf, 2, 3))) static void print_double(double value, const char *fmt, ...) {
    char label[128];
    va_list args;
    va_start(args, fmt);
    sceClibVsnprintf(label, sizeof(label), fmt, args);
    va_end(args);
    const double_bits b = { .d = value };
    char line[180];
    const int len = sceClibSnprintf(line, sizeof(line), "%s: 0x%08X%08X\n", label, (unsigned)(b.u >> 32), (unsigned)b.u);
    test_output(line, len < (int)sizeof(line) ? len : (int)sizeof(line) - 1);
}

static unsigned int float_value(float value) {
    const float_bits b = { .f = value };
    return b.u;
}

static const struct {
    const char *name;
    unsigned long long bits;
} inputs[] = {
    { "0", 0x0000000000000000ull },
    { "-0", 0x8000000000000000ull },
    { "the smallest denormal", 0x0000000000000001ull },
    { "0.5", 0x3FE0000000000000ull },
    { "1", 0x3FF0000000000000ull },
    { "-1", 0xBFF0000000000000ull },
    { "2", 0x4000000000000000ull },
    { "pi", 0x400921FB54442D18ull },
    { "-2.5", 0xC004000000000000ull },
    { "100", 0x4059000000000000ull },
    { "1e22", 0x44B52D02C7E14AF6ull },
    { "the largest double", 0x7FEFFFFFFFFFFFFFull },
    { "infinity", 0x7FF0000000000000ull },
    { "-infinity", 0xFFF0000000000000ull },
    { "NaN", 0x7FF8000000000000ull },
    { "-NaN with a payload", 0xFFF8000000001234ull },
};

static const struct {
    const char *name;
    double (*func)(double);
} unary[] = {
    { "sin", scelibc_sin },     { "cos", scelibc_cos },     { "tan", scelibc_tan },     { "asin", scelibc_asin },
    { "acos", scelibc_acos },   { "atan", scelibc_atan },   { "exp", scelibc_exp },     { "log", scelibc_log },
    { "log10", scelibc_log10 }, { "sqrt", scelibc_sqrt },   { "cbrt", scelibc_cbrt },   { "floor", scelibc_floor },
    { "ceil", scelibc_ceil },   { "round", scelibc_round }, { "trunc", scelibc_trunc }, { "rint", scelibc_rint },
};

static const struct {
    const char *name;
    float (*func)(float);
} unary_float[] = {
    { "sinf", scelibc_sinf }, { "cosf", scelibc_cosf }, { "sqrtf", scelibc_sqrtf },
    { "expf", scelibc_expf }, { "logf", scelibc_logf },
};

static int *errno_loc;

// Prints what call returns and the errno it leaves
#define PRINT_ERRNO(name, call)                    \
    do {                                           \
        *errno_loc = 0;                            \
        print_double(call, "%s", name);            \
        PRINTF(*errno_loc, "%s: errno", name);     \
    } while (0)

int main(void) {
    errno_loc = scelibc__sceLibcErrnoLoc();
    const int count = sizeof(inputs) / sizeof(inputs[0]);

    // One argument, every input
    for (unsigned int f = 0; f < sizeof(unary) / sizeof(unary[0]); f++) {
        for (int i = 0; i < count; i++)
            print_double(unary[f].func(from_bits(inputs[i].bits)), "%s of %s", unary[f].name, inputs[i].name);
    }

    // Floats
    static const struct {
        const char *name;
        unsigned int bits;
    } float_inputs[] = {
        { "0", 0x00000000 },   { "-0", 0x80000000 },       { "the smallest denormal", 0x00000001 },
        { "1", 0x3F800000 },   { "-1", 0xBF800000 },       { "pi", 0x40490FDB },
        { "1e10", 0x501502F9 }, { "infinity", 0x7F800000 }, { "NaN", 0x7FC00000 },
    };
    for (unsigned int f = 0; f < sizeof(unary_float) / sizeof(unary_float[0]); f++) {
        for (unsigned int i = 0; i < sizeof(float_inputs) / sizeof(float_inputs[0]); i++)
            PRINTF(float_value(unary_float[f].func(float_from_bits(float_inputs[i].bits))), "%s of %s", unary_float[f].name, float_inputs[i].name);
    }
    PRINTF(float_value(scelibc_powf(2.0f, 0.5f)), "powf of 2 and 0.5");
    PRINTF(float_value(scelibc_fmodf(10.0f, 3.0f)), "fmodf of 10 and 3");
    PRINTF(float_value(scelibc_atan2f(1.0f, -1.0f)), "atan2f of 1 and -1");

    // Rounding halfway cases
    static const double halves[] = { 0.5, 1.5, 2.5, -0.5, -1.5, -2.5 };
    for (unsigned int i = 0; i < sizeof(halves) / sizeof(halves[0]); i++) {
        PRINTF(scelibc_lround(halves[i]), "lround of %d/2", (int)(halves[i] * 2));
        PRINTF(scelibc_lrint(halves[i]), "lrint of %d/2", (int)(halves[i] * 2));
        print_double(scelibc_rint(halves[i]), "rint of %d/2", (int)(halves[i] * 2));
        print_double(scelibc_round(halves[i]), "round of %d/2", (int)(halves[i] * 2));
    }

    // Two arguments
    const double zero = 0.0, negative_zero = from_bits(0x8000000000000000ull);
    const double inf = from_bits(0x7FF0000000000000ull), nan = from_bits(0x7FF8000000000000ull);
    print_double(scelibc_atan2(zero, zero), "atan2 of 0 and 0");
    print_double(scelibc_atan2(zero, negative_zero), "atan2 of 0 and -0");
    print_double(scelibc_atan2(negative_zero, -1.0), "atan2 of -0 and -1");
    print_double(scelibc_atan2(1.0, -inf), "atan2 of 1 and -infinity");
    print_double(scelibc_atan2(inf, inf), "atan2 of infinity and infinity");
    print_double(scelibc_pow(2.0, 10.0), "pow of 2 and 10");
    print_double(scelibc_pow(2.0, 0.5), "pow of 2 and 0.5");
    print_double(scelibc_pow(-2.0, 3.0), "pow of -2 and 3");
    print_double(scelibc_pow(-8.0, 1.0 / 3.0), "pow of -8 and a third");
    print_double(scelibc_pow(nan, 0.0), "pow of NaN and 0");
    print_double(scelibc_pow(1.0, nan), "pow of 1 and NaN");
    print_double(scelibc_pow(-1.0, inf), "pow of -1 and infinity");
    print_double(scelibc_pow(negative_zero, -3.0), "pow of -0 and -3");
    print_double(scelibc_pow(10.0, -310.0), "pow of 10 and -310");
    print_double(scelibc_fmod(10.0, 3.0), "fmod of 10 and 3");
    print_double(scelibc_fmod(-10.0, 3.0), "fmod of -10 and 3");
    print_double(scelibc_fmod(1e300, 3.0), "fmod of 1e300 and 3");
    print_double(scelibc_fmod(5.0, inf), "fmod of 5 and infinity");
    print_double(scelibc_remainder(5.5, 2.0), "remainder of 5.5 and 2");
    print_double(scelibc_remainder(-7.0, 2.0), "remainder of -7 and 2");
    print_double(scelibc_hypot(3.0, 4.0), "hypot of 3 and 4");
    print_double(scelibc_hypot(inf, nan), "hypot of infinity and NaN");
    print_double(scelibc_hypot(1e300, 1e300), "hypot of 1e300 and 1e300");
    print_double(scelibc_fma(0.1, 10.0, -1.0), "fma of 0.1, 10 and -1");
    print_double(scelibc_nextafter(zero, 1.0), "nextafter of 0 toward 1");
    print_double(scelibc_nextafter(1.0, 2.0), "nextafter of 1 toward 2");
    print_double(scelibc_nextafter(1.0, 1.0), "nextafter of 1 toward 1");
    print_double(scelibc_copysign(1.0, negative_zero), "copysign of 1 and -0");
    print_double(scelibc_copysign(nan, -1.0), "copysign of NaN and -1");

    // Splitting and scaling
    int exponent = 0x55;
    print_double(scelibc_frexp(48.0, &exponent), "frexp of 48");
    PRINT("its exponent", exponent);
    exponent = 0x55;
    print_double(scelibc_frexp(from_bits(1), &exponent), "frexp of the smallest denormal");
    PRINT("its exponent", exponent);
    exponent = 0x55;
    print_double(scelibc_frexp(zero, &exponent), "frexp of 0");
    PRINT("its exponent", exponent);
    exponent = 0x55;
    print_double(scelibc_frexp(inf, &exponent), "frexp of infinity");
    PRINT("its exponent", exponent);
    double integer = 0;
    print_double(scelibc_modf(-3.75, &integer), "modf of -3.75");
    print_double(integer, "its integer part");
    print_double(scelibc_modf(inf, &integer), "modf of infinity");
    print_double(integer, "its integer part");
    print_double(scelibc_ldexp(1.0, 1023), "ldexp of 1 and 1023");
    print_double(scelibc_ldexp(1.0, 1024), "ldexp of 1 and 1024");
    print_double(scelibc_ldexp(1.0, -1074), "ldexp of 1 and -1074");
    print_double(scelibc_ldexp(1.0, -1075), "ldexp of 1 and -1075");
    print_double(scelibc_ldexp(3.0, -1074), "ldexp of 3 and -1074");

    // Domain and range errors
    PRINT_ERRNO("log of -1", scelibc_log(-1.0));
    PRINT_ERRNO("log of 0", scelibc_log(zero));
    PRINT_ERRNO("sqrt of -1", scelibc_sqrt(-1.0));
    PRINT_ERRNO("acos of 2", scelibc_acos(2.0));
    PRINT_ERRNO("exp of 1000", scelibc_exp(1000.0));
    PRINT_ERRNO("exp of -1000", scelibc_exp(-1000.0));
    PRINT_ERRNO("pow of 0 and -1", scelibc_pow(zero, -1.0));
    PRINT_ERRNO("pow of -2 and 0.5", scelibc_pow(-2.0, 0.5));
    PRINT_ERRNO("pow of 10 and 400", scelibc_pow(10.0, 400.0));
    PRINT_ERRNO("fmod of 1 and 0", scelibc_fmod(1.0, zero));
    PRINT_ERRNO("fmod of infinity and 1", scelibc_fmod(inf, 1.0));
    PRINT_ERRNO("ldexp of 1 and 2000", scelibc_ldexp(1.0, 2000));
    PRINT_ERRNO("sqrt of NaN", scelibc_sqrt(nan));

    test_exit();
    return 0;
}
