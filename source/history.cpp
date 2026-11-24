#include "history.h"

int32_t dayNumber(int y, int m, int d) {
    // Howard Hinnant's days_from_civil
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

int32_t dayFromPosix(uint64_t t, int64_t off) {
    int64_t s = (int64_t)t + off;
    return (int32_t)(s >= 0 ? s / 86400 : (s - 86399) / 86400);
}

DayStats dayStats(const std::set<int32_t>& days) {
    DayStats st;
    st.days = (int)days.size();
    int run = 0;
    int32_t prev = 0;
    bool first = true;
    for (int32_t d : days) {
        if (!first && d == prev + 1) run++;
        else {
            if (!first && d - prev - 1 > st.longestGap) st.longestGap = d - prev - 1;
            run = 1;
        }
        if (run > st.longestStreak) st.longestStreak = run;
        prev = d;
        first = false;
    }
    return st;
}

void addSessionDays(std::set<int32_t>& out, uint64_t a, uint64_t b, int64_t off) {
    if (b < a) { uint64_t t = a; a = b; b = t; }
    if (a == 0) a = b;
    if (a == 0) return;
    int32_t da = dayFromPosix(a, off), db = dayFromPosix(b, off);
    if (db - da > 2) db = da;   // bogus range (clock change, missing exit event)
    for (int32_t d = da; d <= db; d++) out.insert(d);
}
