// Save captures: copies of a game's whole save, taken by the live module each time the game closes
// (a running game keeps its save locked, so "on close" is the only moment it can be read), for the
// games the player turned recording on for in the app. The app serves them over Wi-Fi to the Save Lab.
//
// Layout (in the data folder, sdmc:/switch/TrophyNX):
//   captures/settings.json                 {"games": ["0100535012974000", ...], "keep": 20}
//   captures/<TITLEID>/<YYYYMMDD-HHMMSS>/  the save files, same tree as inside the save
//   captures/<TITLEID>/<YYYYMMDD-HHMMSS>.json  {"title_id","name","time","session_minutes","bytes","files":[{"path","size","crc32"}]}
// Shared by the app and the module (sysmodule/source/shared_captures.cpp).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct CaptureSettings {
    std::vector<uint64_t> games;   // titles to capture on close
    int keep = 20;                 // captures kept per game (oldest deleted first)
};

struct CaptureInfo {
    std::string stamp;        // folder name, YYYYMMDD-HHMMSS (UTC)
    uint64_t time = 0;        // POSIX time of the capture
    int sessionMinutes = 0;   // length of the play session that ended with this capture
    uint64_t bytes = 0;
    int files = 0;
};

enum CaptureOutcome { CAPTURE_SAVED, CAPTURE_UNCHANGED, CAPTURE_EMPTY, CAPTURE_TOO_BIG, CAPTURE_FAILED };

static const uint64_t CAPTURE_MAX_BYTES = 128ull << 20;   // a bigger save is not captured
static const int CAPTURE_KEEP_MAX = 200;

std::string capturesDir(const std::string& dataDir);
CaptureSettings loadCaptureSettings(const std::string& dataDir);
bool saveCaptureSettings(const std::string& dataDir, const CaptureSettings& s);
bool captureEnabled(const CaptureSettings& s, uint64_t titleId);
void setCaptureEnabled(CaptureSettings& s, uint64_t titleId, bool on);

// Copies every file under srcRoot (e.g. "acsave:/") into a new capture. A capture identical to the
// previous one (same files, sizes and CRC32) is not kept (CAPTURE_UNCHANGED). Then trims to `keep`.
// msg: one line for the log.
CaptureOutcome captureFromDir(const std::string& srcRoot, const std::string& dataDir, uint64_t titleId,
                              const std::string& name, uint64_t now, int sessionMinutes, int keep, std::string& msg);

std::vector<CaptureInfo> listCaptures(const std::string& dataDir, uint64_t titleId);   // oldest first
int deleteCaptures(const std::string& dataDir, uint64_t titleId);                     // number deleted
// Titles that have at least one capture.
std::vector<uint64_t> capturedTitles(const std::string& dataDir);

// captures.json served to the Save Lab: every game with captures, each capture with its files.
// names: display names by title id (falls back to the name stored in the capture).
std::string capturesIndexJson(const std::string& dataDir, const std::map<uint64_t, std::string>& names);

// Path checks for the Wi-Fi server: a file inside one capture, nothing else.
bool captureFilePath(const std::string& dataDir, const std::string& tidHex, const std::string& stamp,
                     const std::string& rel, std::string& out);
