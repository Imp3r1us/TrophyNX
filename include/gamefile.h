#pragma once
// A file of an installed game (its romfs), read in pieces: game files that hold save passwords
// (resources.assets, global-metadata.dat) are tens of MB, more than the live module's heap.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

// read(offset, buffer, length): false on a read error or past the end.
typedef std::function<bool(uint64_t, void*, size_t)> ByteReader;

struct GameFile {
    uint64_t size = 0;
    ByteReader read;
    std::function<void()> close;   // releases the file and its file system (always set when open succeeded)
};

// Opens <rel> ("Data/resources.assets") in the game's romfs. false + why (every attempt and its result) when
// the system refuses or the file isn't there.
typedef std::function<bool(uint64_t titleId, const std::string& rel, GameFile& out, std::string& why)> GameFileOpener;

// Switch only (source/gamefile_switch.cpp, shared by the app and the live module).
bool openGameFile(uint64_t titleId, const std::string& rel, GameFile& out, std::string& why);
