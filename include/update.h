// In-app updater: downloads the latest build from the repository's "builds" branch.
// Works without any setup (public repository). Optional sdmc:/switch/TrophyNX/update.json:
//   { "repo": "owner/name", "branch": "builds", "token": "github_pat_..." }
// for a fork, another branch, or a private copy (token = fine-grained, Contents: read-only).
// CI publishes version.json + the SD layout under sd/ on that branch.
#pragma once
#include "platform.h"
#include <cstdint>
#include <string>
#include <vector>

struct UpdateFile {
    std::string path;    // SD-relative path in the build (switch/TrophyNX.nro, ...)
    std::string local;   // where it is installed here (the running .nro may live elsewhere)
    std::string sha256;
    uint64_t size = 0;
    bool changed = false;
};

struct UpdateInfo {
    enum State { Error, UpToDate, Available } state = Error;
    std::string error;
    std::string commit, date, message, appVersion, moduleVersion;
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
