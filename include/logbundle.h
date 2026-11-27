// SPDX-License-Identifier: GPL-2.0-or-later
// "Send logs" (debug screen, Y): every TrophyNX log in one HTTP POST to a receiver on your network
// (tools/logserver/trophynx_logserver.py, e.g. on a Raspberry Pi). The receiver's address is read from
// <data dir>/log_server.txt (one line: "192.168.1.20" or "192.168.1.20:8421"), so it never ships with the app.
//
// Bundle format (plain bytes, easy to read back):
//   "TNXLOGS1\n" then, per file: "<name>\t<size>\n" + <size> bytes + "\n"
// Names are relative paths ("live_log.txt", "overlay/0100633007D48000.memtest.json", "hook/<TID>.json").
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace logbundle {

constexpr int kDefaultPort = 8421;
constexpr size_t kMaxPerFile = 1024 * 1024;   // bigger logs: only their end (the newest lines) is sent

struct File {
    std::string name;
    std::string data;
    bool truncated = false;   // only the last kMaxPerFile bytes
};

// The logs and status files worth sending: the data folder's logs, the overlay's live files and the hook
// manifests in atmosphere/contents/*/trophynx-hook.json. `info` (app version, time...) becomes "info.txt".
std::vector<File> collect(const std::string& dataDir, const std::string& sdRoot, const std::string& info,
                          size_t maxPerFile = kMaxPerFile);

std::string encode(const std::vector<File>& files);
bool decode(const std::string& bundle, std::vector<File>& out);   // for tests and tools

// "192.168.1.20", "192.168.1.20:8421", "http://pi.local:9000" -> "http://<host>:<port>/trophynx/logs".
// False (with err) for an empty or malformed line. Comments (#...) and blank lines are skipped.
bool serverUrl(const std::string& fileText, std::string& url, std::string& err);

// A safe relative name: letters, digits, . _ - and / between parts, no "..", no leading /.
bool safeName(const std::string& name);

}  // namespace logbundle
