#pragma once
// Compatibility scan (ZR on the Overview): for every installed game, mounts the save read-only, lists its
// files, guesses each file's format and writes, in the data folder:
//   scan_bundle.json  machine-readable report + copies of readable saves (base64), for
//                     tools/pc2switch/draft.js and Save Lab — one file to upload from a phone
//   scan_report.txt   the same verdicts in plain text
// Nothing is modified on the console besides those two files.
#include "achievements.h"
#include <string>

struct ScanSummary {
    int games = 0, withSave = 0, supported = 0, draftable = 0, text = 0, binary = 0, encrypted = 0;
    size_t bytesCopied = 0;
    std::string bundlePath, error;
};

// Format of one file's bytes: "json", "byml", "xml", "text", "compressed", "encrypted", "binary", "empty".
std::string scanFormat(const std::vector<uint8_t>& data, double* entropyOut = nullptr);
// Game verdict from its files' formats and whether a save-based set already exists.
std::string scanVerdict(const std::vector<std::string>& formats, bool hasReader);

bool runCompatibilityScan(Library& lib, plat::ProgressFn progress, void* ctx, ScanSummary& out);
