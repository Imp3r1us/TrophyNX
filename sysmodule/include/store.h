// Unlock store shared with the app: sdmc:/switch/SwitchAchievements/unlocks/<user>.json,
// key "TITLEID/achId" -> unix time of the unlock.
#pragma once
#include <switch.h>
#include <cstdint>
#include <map>
#include <string>

extern const char* const DATA_DIR;   // sdmc:/switch/SwitchAchievements

// Hex key of a user, same format as the app's unlock files.
std::string userKeyOf(const AccountUid& uid);
// Last user who opened a game ("" if none): the one playing / who just played.
std::string lastUser(AccountUid* uidOut = nullptr);
std::map<std::string, uint64_t> loadStore(const std::string& user);
// Written to a temporary file, then renamed over the old one.
void saveStore(const std::string& user, const std::map<std::string, uint64_t>& m);
