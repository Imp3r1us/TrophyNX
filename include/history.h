#pragma once
// Play history and album: per-game days played (system play log, pdm) and screenshots / videos
// (album, caps:a). Portable helpers here; the platform layer fills the data.
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

struct PlayHistory {
    bool valid = false;
    std::string source;                          // which log was read (shown in log.txt)
    std::map<uint64_t, std::set<int32_t>> days;  // title id -> local day numbers with play
    std::vector<std::string> notes;              // diagnostics for log.txt
};

struct AlbumCounts {
    bool valid = false;
    std::map<uint64_t, uint32_t> screenshots, videos;
    std::vector<std::string> notes;
};

struct DayStats {
    int days = 0;           // distinct days
    int longestStreak = 0;  // most consecutive days
    int longestGap = 0;     // most days away between two play days (0 = never left)
};

// Days since 1970-01-01 for a civil date (proleptic Gregorian).
int32_t dayNumber(int year, int month, int day);
// Local day of a POSIX time, given the local UTC offset in seconds.
int32_t dayFromPosix(uint64_t t, int64_t utcOffsetSec);
DayStats dayStats(const std::set<int32_t>& days);
// Adds every day from a to b (session crossing midnight); spans over 2 days are treated as one day (a).
void addSessionDays(std::set<int32_t>& out, uint64_t a, uint64_t b, int64_t utcOffsetSec);
