// The rest of the ScePaf calls that need nothing but the load: the local time against the RTC's, and
// setjmp with longjmp
#include <psp2/rtc.h>

#include "paf.h"

static int jump_env[64];

static void jump_back(int value) {
    sce_paf_longjmp(jump_env, value);
}

int main(void) {
    paf_load_or_skip();

    // The local time
    ScePafDateTime paf_time;
    sce_paf_memset(&paf_time, 0xAA, sizeof(paf_time));
    PRINT("get the local time fails", scePafGetCurrentClockLocalTime(&paf_time) < 0);
    SceDateTime rtc_time;
    SceRtcTick tick, local;
    sceRtcGetCurrentClock(&rtc_time, 0);
    sceRtcGetCurrentTick(&tick);
    sceRtcConvertUtcToLocalTime(&tick, &local);
    SceDateTime local_date;
    sceRtcSetTick(&local_date, &local);
    PRINT("the year matches the RTC's local year", paf_time.data.year == local_date.year);
    PRINT("the month is valid", paf_time.data.month >= 1 && paf_time.data.month <= 12);
    PRINT("the day is valid", paf_time.data.day >= 1 && paf_time.data.day <= 31);
    PRINT("the time is valid", paf_time.data.hour < 24 && paf_time.data.minute < 60 && paf_time.data.second < 60);
    PRINT("the microseconds are valid", paf_time.data.microsecond < 1000000);
    PRINT("the day matches the RTC's local day", paf_time.data.day == local_date.day || paf_time.data.hour == 23 || paf_time.data.hour == 0);
    PRINT("the size of the date time", sizeof(ScePafDateTime));

    // setjmp and longjmp
    volatile int count = 0;
    const int value = sce_paf_setjmp(jump_env);
    count++;
    if (count == 1) {
        PRINT("setjmp returns 0 at first", value);
        jump_back(5);
    } else if (count == 2) {
        PRINT("longjmp returns the value to setjmp", value);
        jump_back(0);
    } else {
        PRINT("longjmp with 0 returns", value);
    }
    PRINT("setjmp ran this many times", count);

    // The local time into NULL crashes a Vita, so it isn't tried

    test_exit();
    return 0;
}
