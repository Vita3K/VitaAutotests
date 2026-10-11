// Date and tick math: leap years, days and weekdays, validation, conversions between ticks, dates,
// time_t, DOS and Win32 times, adding to ticks, and formatting and parsing. Only the current time
// calls depend on the clock, and they print comparisons.
#include <psp2/rtc.h>

#include "test.h"

// Prints a date's fields on one line, under a label that holds them
static void print_date(const char *what, const SceDateTime *d) {
    PRINTF(0, "%s: %04u-%02u-%02u %02u:%02u:%02u.%06u", what, d->year, d->month, d->day, d->hour, d->minute, d->second, d->microsecond);
}

// Prints a tick as the date it is
static void print_tick(const char *what, const SceRtcTick *tick) {
    SceDateTime d = { 0 };
    const int ret = sceRtcSetTick(&d, tick);
    if (ret < 0) {
        PRINTF(ret, "%s: set the date from the tick", what);
        return;
    }
    print_date(what, &d);
}

int main(void) {
    PRINT("tick resolution", sceRtcGetTickResolution());

    // Calendar
    static const int years[] = { 1900, 2000, 2004, 2023, 2024, 0, 1, -1, 9999, 10000 };
    for (unsigned int i = 0; i < sizeof(years) / sizeof(years[0]); i++)
        PRINTF(sceRtcIsLeapYear(years[i]), "%d is a leap year", years[i]);
    static const int months[][2] = { { 2024, 2 }, { 2023, 2 }, { 1900, 2 }, { 2024, 4 }, { 2024, 12 }, { 2024, 0 }, { 2024, 13 }, { 0, 1 }, { 10000, 1 } };
    for (unsigned int i = 0; i < sizeof(months) / sizeof(months[0]); i++)
        PRINTF(sceRtcGetDaysInMonth(months[i][0], months[i][1]), "days in %d-%d", months[i][0], months[i][1]);
    static const int days[][3] = { { 2026, 10, 11 }, { 1970, 1, 1 }, { 1, 1, 1 }, { 2000, 2, 29 }, { 2023, 2, 29 }, { 2024, 13, 1 }, { 2024, 1, 0 }, { 9999, 12, 31 } };
    for (unsigned int i = 0; i < sizeof(days) / sizeof(days[0]); i++)
        PRINTF(sceRtcGetDayOfWeek(days[i][0], days[i][1], days[i][2]), "day of the week of %d-%d-%d", days[i][0], days[i][1], days[i][2]);

    // Validation
    const SceDateTime base = { 2026, 10, 11, 12, 34, 56, 789012 };
    PRINT("check a valid date", sceRtcCheckValid(&base));
    static const struct {
        const char *what;
        SceDateTime date;
    } invalid[] = {
        { "year 0", { 0, 1, 1, 0, 0, 0, 0 } },
        { "year 10000", { 10000, 1, 1, 0, 0, 0, 0 } },
        { "month 0", { 2026, 0, 1, 0, 0, 0, 0 } },
        { "month 13", { 2026, 13, 1, 0, 0, 0, 0 } },
        { "day 0", { 2026, 1, 0, 0, 0, 0, 0 } },
        { "day 32", { 2026, 1, 32, 0, 0, 0, 0 } },
        { "February 29 of a common year", { 2023, 2, 29, 0, 0, 0, 0 } },
        { "hour 24", { 2026, 1, 1, 24, 0, 0, 0 } },
        { "minute 60", { 2026, 1, 1, 0, 60, 0, 0 } },
        { "second 60", { 2026, 1, 1, 0, 0, 60, 0 } },
        { "microsecond 1000000", { 2026, 1, 1, 0, 0, 0, 1000000 } },
    };
    for (unsigned int i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        PRINTF(sceRtcCheckValid(&invalid[i].date), "check %s", invalid[i].what);

    // Ticks and dates
    SceRtcTick tick = { 0 };
    PRINT("get the tick of a date", sceRtcGetTick(&base, &tick));
    PRINT("its high word", (SceUInt32)(tick.tick >> 32));
    PRINT("its low word", (SceUInt32)tick.tick);
    print_tick("back to a date", &tick);
    const SceDateTime first = { 1, 1, 1, 0, 0, 0, 0 };
    SceRtcTick zero = { 0xDEAD };
    PRINT("get the tick of 0001-01-01", sceRtcGetTick(&first, &zero));
    PRINT("it is 0", zero.tick == 0);
    SceRtcTick bad_tick = { 0 };
    PRINT("get the tick of an invalid date", sceRtcGetTick(&invalid[3].date, &bad_tick));
    SceRtcTick last = { 0xFFFFFFFFFFFFFFFFull };
    SceDateTime date = { 0 };
    PRINT("set a date from the largest tick", sceRtcSetTick(&date, &last));

    // time_t, DOS and Win32 times
    PRINT("set a date from time_t 0", sceRtcSetTime_t(&date, 0));
    print_date("time_t 0", &date);
    PRINT("set a date from time_t 0x7FFFFFFF", sceRtcSetTime_t(&date, 0x7FFFFFFF));
    print_date("time_t 0x7FFFFFFF", &date);
    time_t t = 0;
    PRINT("get time_t of the date", sceRtcGetTime_t(&base, &t));
    PRINT("it", (int)t);
    SceUInt64 t64 = 0;
    PRINT("get 64 bit time_t of the date", sceRtcGetTime64_t(&base, &t64));
    PRINT("it", (int)t64);
    PRINT("get time_t of a date before 1970", sceRtcGetTime_t(&first, &t));
    PRINT("set a date from 64 bit time_t 0", sceRtcSetTime64_t(&date, 0));
    print_date("64 bit time_t 0", &date);
    unsigned int dos = 0;
    PRINT("get the DOS time of the date", sceRtcGetDosTime(&base, &dos));
    PRINT("it", dos);
    PRINT("set a date from that DOS time", sceRtcSetDosTime(&date, dos));
    print_date("DOS time", &date);
    PRINT("get the DOS time of a date before 1980", sceRtcGetDosTime(&first, &dos));
    SceUInt64 win32 = 0;
    PRINT("get the Win32 time of the date", sceRtcGetWin32FileTime(&base, &win32));
    PRINT("its high word", (SceUInt32)(win32 >> 32));
    PRINT("its low word", (SceUInt32)win32);
    PRINT("set a date from Win32 time 0", sceRtcSetWin32FileTime(&date, 0));
    print_date("Win32 time 0", &date);

    // Adding
    SceRtcTick out = { 0 };
    PRINT("add 1 tick", sceRtcTickAddTicks(&out, &tick, 1));
    print_tick("after 1 tick", &out);
    PRINT("add a million microseconds", sceRtcTickAddMicroseconds(&out, &tick, 1000000));
    print_tick("after a million microseconds", &out);
    PRINT("add -57 seconds", sceRtcTickAddSeconds(&out, &tick, -57));
    print_tick("after -57 seconds", &out);
    PRINT("add 30 minutes", sceRtcTickAddMinutes(&out, &tick, 30));
    print_tick("after 30 minutes", &out);
    PRINT("add 12 hours", sceRtcTickAddHours(&out, &tick, 12));
    print_tick("after 12 hours", &out);
    PRINT("add 100 days", sceRtcTickAddDays(&out, &tick, 100));
    print_tick("after 100 days", &out);
    PRINT("add -2 weeks", sceRtcTickAddWeeks(&out, &tick, -2));
    print_tick("after -2 weeks", &out);
    const SceDateTime jan31 = { 2024, 1, 31, 0, 0, 0, 0 };
    SceRtcTick jan31_tick = { 0 };
    sceRtcGetTick(&jan31, &jan31_tick);
    PRINT("add 1 month to January 31", sceRtcTickAddMonths(&out, &jan31_tick, 1));
    print_tick("after 1 month", &out);
    PRINT("add 13 months to January 31", sceRtcTickAddMonths(&out, &jan31_tick, 13));
    print_tick("after 13 months", &out);
    const SceDateTime feb29 = { 2024, 2, 29, 0, 0, 0, 0 };
    SceRtcTick feb29_tick = { 0 };
    sceRtcGetTick(&feb29, &feb29_tick);
    PRINT("add 1 year to February 29", sceRtcTickAddYears(&out, &feb29_tick, 1));
    print_tick("after 1 year", &out);
    PRINT("add -2025 years", sceRtcTickAddYears(&out, &tick, -2025));
    print_tick("after -2025 years", &out);
    PRINT("add 8000 years", sceRtcTickAddYears(&out, &tick, 8000));
    PRINT("subtract a tick from 0", sceRtcTickAddTicks(&out, &zero, -1));

    // Comparing
    SceRtcTick later = { tick.tick + 1 };
    PRINT("compare a tick with a later one", sceRtcCompareTick(&tick, &later));
    PRINT("compare it with itself", sceRtcCompareTick(&tick, &tick));
    PRINT("compare a later one with it", sceRtcCompareTick(&later, &tick));

    // Formatting and parsing
    char text[64];
    sceClibMemset(text, 0, sizeof(text));
    PRINTF(sceRtcFormatRFC3339(text, &tick, 0), "format RFC 3339 in UTC: %s", text);
    sceClibMemset(text, 0, sizeof(text));
    PRINTF(sceRtcFormatRFC3339(text, &tick, 540), "format RFC 3339 at +9:00: %s", text);
    sceClibMemset(text, 0, sizeof(text));
    PRINTF(sceRtcFormatRFC3339(text, &tick, -90), "format RFC 3339 at -1:30: %s", text);
    sceClibMemset(text, 0, sizeof(text));
    PRINTF(sceRtcFormatRFC2822(text, &tick, 0), "format RFC 2822 in UTC: %s", text);
    sceClibMemset(text, 0, sizeof(text));
    PRINTF(sceRtcFormatRFC2822(text, &tick, 540), "format RFC 2822 at +9:00: %s", text);
    SceRtcTick parsed = { 0 };
    PRINT("parse RFC 3339 in UTC", sceRtcParseRFC3339(&parsed, "2026-10-11T12:34:56.789012Z"));
    PRINT("it is the tick of the date", parsed.tick == tick.tick);
    PRINT("parse RFC 3339 at +9:00", sceRtcParseRFC3339(&parsed, "2026-10-11T21:34:56+09:00"));
    print_tick("parsed at +9:00", &parsed);
    PRINT("parse RFC 3339 without a zone", sceRtcParseRFC3339(&parsed, "2026-10-11T12:34:56"));
    PRINT("parse a date only", sceRtcParseRFC3339(&parsed, "2026-10-11"));
    PRINT("parse garbage", sceRtcParseRFC3339(&parsed, "not a date"));
    PRINT("parse an empty string", sceRtcParseRFC3339(&parsed, ""));
    PRINT("parse an RFC 2822 date", sceRtcParseDateTime(&parsed, "Sun, 11 Oct 2026 12:34:56 +0000"));
    print_tick("parsed RFC 2822", &parsed);
    PRINT("parse an RFC 3339 date as a date and time", sceRtcParseDateTime(&parsed, "2026-10-11T12:34:56Z"));
    print_tick("parsed as a date and time", &parsed);
    PRINT("parse garbage as a date and time", sceRtcParseDateTime(&parsed, "garbage"));

    // The current time, as comparisons
    SceRtcTick now = { 0 };
    PRINT("get the current tick", sceRtcGetCurrentTick(&now));
    const SceDateTime y2020 = { 2020, 1, 1, 0, 0, 0, 0 };
    SceRtcTick y2020_tick = { 0 };
    sceRtcGetTick(&y2020, &y2020_tick);
    PRINT("it is after 2020", now.tick > y2020_tick.tick);
    SceDateTime clock = { 0 };
    PRINT("get the current clock in UTC", sceRtcGetCurrentClock(&clock, 0));
    PRINT("its year is after 2020", clock.year > 2020);
    PRINT("get the current clock in local time", sceRtcGetCurrentClockLocalTime(&clock));
    SceRtcTick local = { 0 }, utc = { 0 };
    PRINT("convert the tick to local time", sceRtcConvertUtcToLocalTime(&now, &local));
    PRINT("and back", sceRtcConvertLocalTimeToUtc(&local, &utc));
    PRINT("it is the same tick", utc.tick == now.tick);

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("get the tick into a NULL pointer", sceRtcGetTick(&base, NULL));
    PRINT("get the tick of a NULL date", sceRtcGetTick(NULL, &tick));
    PRINT("check a NULL date", sceRtcCheckValid(NULL));
    PRINT("get the current tick into a NULL pointer", sceRtcGetCurrentTick(NULL));
    PRINT("compare with a NULL tick", sceRtcCompareTick(&tick, NULL));
    PRINT("format into a NULL buffer", sceRtcFormatRFC3339(NULL, &tick, 0));
    PRINT("parse a NULL string", sceRtcParseRFC3339(&parsed, NULL));

    test_exit();
    return 0;
}
