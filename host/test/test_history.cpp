// Play-history helpers: day numbers, streaks, breaks, sessions across midnight.
#include "history.h"
#include <cstdio>
#include <cstdlib>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    CHECK(dayNumber(1970, 1, 1) == 0);
    CHECK(dayNumber(2000, 3, 1) == 11017);
    CHECK(dayNumber(2026, 11, 24) - dayNumber(2025, 11, 24) == 365);
    CHECK(dayNumber(2024, 3, 1) - dayNumber(2024, 2, 28) == 2);   // leap year

    // 2026-11-24 23:30 UTC = 2026-11-25 00:30 in Paris (UTC+1)
    uint64_t t = (uint64_t)dayNumber(2026, 11, 24) * 86400 + 23 * 3600 + 1800;
    CHECK(dayFromPosix(t, 0) == dayNumber(2026, 11, 24));
    CHECK(dayFromPosix(t, 3600) == dayNumber(2026, 11, 25));
    CHECK(dayFromPosix(1800, -3600) == -1);

    std::set<int32_t> s;
    DayStats st = dayStats(s);
    CHECK(st.days == 0 && st.longestStreak == 0 && st.longestGap == 0);
    s = {10};
    st = dayStats(s);
    CHECK(st.days == 1 && st.longestStreak == 1 && st.longestGap == 0);
    s = {1, 2, 3, 10, 11, 12, 13, 200};
    st = dayStats(s);
    CHECK(st.days == 8);
    CHECK(st.longestStreak == 4);
    CHECK(st.longestGap == 186);   // 14..199

    // Session 23:00 -> 01:00 counts both days; a bogus 10-day session counts its first day only.
    std::set<int32_t> d;
    uint64_t a = (uint64_t)dayNumber(2026, 1, 1) * 86400 + 23 * 3600;
    addSessionDays(d, a, a + 7200, 0);
    CHECK(d.size() == 2);
    d.clear();
    addSessionDays(d, a, a + 10 * 86400, 0);
    CHECK(d.size() == 1);
    d.clear();
    addSessionDays(d, a + 7200, a, 0);   // reversed
    CHECK(d.size() == 2);
    d.clear();
    addSessionDays(d, 0, 0, 0);
    CHECK(d.empty());

    printf(fails ? "test_history: %d failures\n" : "test_history: all OK\n", fails);
    return fails ? 1 : 0;
}
