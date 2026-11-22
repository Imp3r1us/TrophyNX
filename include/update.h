// In-app updater. By default it installs the latest GitHub Release of the repository: CI publishes one
// for every new version (tag vX.Y.Z) with version.json + each file as an asset. No setup needed.
// Optional sdmc:/switch/TrophyNX/update.json:
//   { "channel": "builds", "repo": "owner/name", "branch": "builds", "token": "github_pat_..." }
//   channel "builds" = every build of main (test builds), read from the "builds" branch (sd/ + version.json).
//   token only for a private copy (fine-grained, Contents: read-only).
#pragma once
#include "platform.h"
#include <cstdint>
#include <string>
#include <vector>

struct UpdateFile {
    std::string path;    // SD-relative path in the build (switch/TrophyNX.nro, ...)
    std::string local;   // where it is installed here (the running .nro may live elsewhere)
    std::string sha256;
    std::string url;     // where to download it (release asset or file of the builds branch)
    uint64_t size = 0;
    bool changed = false;
};

struct UpdateInfo {
    enum State { Error, UpToDate, Available } state = Error;
    std::string error;
    std::string commit, date, message, appVersion, moduleVersion;
    std::vector<std::string> notes;   // what this version changes (CHANGELOG.md bullets, version.json "notes")
    std::string source;          // "release v0.9.1" or "builds branch"
    bool fromRelease = false;
    std::vector<UpdateFile> files;
    uint64_t downloadBytes = 0;
    bool appChanged = false, moduleChanged = false;
    std::string configPath;
};

// Reads update.json, fetches version.json and compares every file with the installed one (SHA-256).
UpdateInfo checkForUpdate();
// Downloads the changed files (to <file>.new, checked against the manifest), then swaps them in.
// Nothing is replaced unless every download succeeded. Restarts the live module if it changed.
// `summary` describes what happened (also on failure).
bool applyUpdate(const UpdateInfo& info, plat::ProgressFn progress, void* ctx, std::string& summary);
