// Live module's Hades reader (sgbToJsonStream): read in pieces, LZ4 through a 64 KB window, only the rule values
// built. Checked against the app's full reader (sgbToJson) on synthetic saves made here (luabins + a real LZ4
// compressor, so matches, overlaps and window wrap-around are exercised), then through the rule engine: every
// Hades achievement must get the same value from the module path (streaming source, small heap) as from the app.
//   g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_sgb_stream.cpp source/rules.cpp source/memrules.cpp source/fileutil.cpp \
//       source/save_formats.cpp source/json_doc.cpp source/byml.cpp -x c source/cJSON.c -o /tmp/test_sgb_stream
//   /tmp/test_sgb_stream romfs/games
#include "rules.h"
#include "save_formats.h"
#include "cJSON.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static int fails = 0;
static void ok(bool c, const char* m) {
    if (!c) fails++;
    printf("%s %s\n", c ? "ok  " : "FAIL", m);
}

// ---------------------------------------------------------------- luabins writer
struct Lua {
    std::vector<uint8_t> b;
    void u32(uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); }
    void nil() { b.push_back('-'); }
    void boolean(bool v) { b.push_back(v ? '1' : '0'); }
    void num(double v) { b.push_back('N'); uint8_t x[8]; memcpy(x, &v, 8); b.insert(b.end(), x, x + 8); }
    void str(const std::string& s) { b.push_back('S'); u32((uint32_t)s.size()); b.insert(b.end(), s.begin(), s.end()); }
    void table(uint32_t pairs) { b.push_back('T'); u32(0); u32(pairs); }
};

// ---------------------------------------------------------------- LZ4 block compressor (greedy, hash of 4 bytes)
static std::vector<uint8_t> lz4Compress(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out;
    std::vector<int> table(1 << 16, -1);
    size_t n = in.size(), anchor = 0, i = 0;
    auto len = [&](size_t v) { while (v >= 255) { out.push_back(255); v -= 255; } out.push_back((uint8_t)v); };
    auto emit = [&](size_t litEnd, size_t off, size_t mlen) {
        size_t lit = litEnd - anchor;
        size_t ml = mlen ? mlen - 4 : 0;
        out.push_back((uint8_t)((lit >= 15 ? 15 : lit) << 4 | (mlen ? (ml >= 15 ? 15 : ml) : 0)));
        if (lit >= 15) len(lit - 15);
        out.insert(out.end(), in.begin() + anchor, in.begin() + litEnd);
        if (!mlen) return;
        out.push_back((uint8_t)off);
        out.push_back((uint8_t)(off >> 8));
        if (ml >= 15) len(ml - 15);
    };
    while (n >= 13 && i + 12 < n) {   // the last 5 bytes are literals, the last match starts 12 bytes before the end
        uint32_t seq;
        memcpy(&seq, &in[i], 4);
        uint32_t h = (seq * 2654435761u) >> 16;
        int cand = table[h];
        table[h] = (int)i;
        if (cand >= 0 && i - cand <= 65535 && memcmp(&in[cand], &in[i], 4) == 0) {
            size_t m = 4;
            while (i + m + 5 < n && in[cand + m] == in[i + m]) m++;
            emit(i, i - cand, m);
            i += m;
            anchor = i;
        } else {
            i++;
        }
    }
    emit(n, 0, 0);
    return out;
}

static std::vector<uint8_t> sgbFile(const std::vector<uint8_t>& state, uint32_t runs) {
    Lua h;
    h.b = {'S', 'G', 'B', '1', 0, 0, 0, 0};
    h.u32(16);
    h.u32(0); h.u32(0);                       // timestamp
    std::string loc = "Location_Tartarus";
    h.u32((uint32_t)loc.size()); h.b.insert(h.b.end(), loc.begin(), loc.end());
    h.u32(runs); h.u32(7); h.u32(3);          // runs, meta, shrine
    h.b.push_back(0); h.b.push_back(1);       // god, hell
    h.u32(1); h.u32(4); h.b.insert(h.b.end(), {'R', 'o', 'o', 'm'});
    h.u32(7); h.b.insert(h.b.end(), {'B', '_', 'I', 'n', 't', 'r', 'o'});
    h.u32(0);                                 // start_next_map
    std::vector<uint8_t> z = lz4Compress(state);
    h.u32((uint32_t)z.size());
    h.b.insert(h.b.end(), z.begin(), z.end());
    return h.b;
}

// A Hades-like state: what the 41 rules read, plus a lot of filler (repeated, so LZ4 finds matches; > 64 KB).
static std::vector<uint8_t> hadesState(bool tartarusCleared, int filler, bool numberFirst) {
    Lua L;
    L.b.push_back(numberFirst ? 3 : 2);
    if (numberFirst) L.num(12.5);              // a global that is not a table comes first
    L.table(6);
    L.str("GameState");
    L.table(6);
    L.str("RoomCountCache");
    L.table(tartarusCleared ? 4 : 3);
    L.str("A_Intro"); L.num(9);
    L.str("A_Boss01"); L.num(9);
    L.str("RoomOpeningWeird\0Key"); L.num(1);   // key with a NUL: cut like cJSON does
    if (tartarusCleared) { L.str("B_Intro"); L.num(1); }
    L.str("QuestStatus");
    L.table(3);
    L.str("WeaponClears"); L.str("CashedOut");
    L.str("MetaUpgradesUnlocked"); L.str("Complete");
    L.str("Codex"); L.str("");
    L.str("WeaponUnlocks");
    L.table(2);
    L.str("SwordWeapon"); L.table(2); L.str("SwordWeapon2"); L.boolean(true); L.str("SwordWeapon3"); L.boolean(false);
    L.str("BowWeapon"); L.table(1); L.str("BowWeapon2"); L.boolean(true);
    L.str("TotalCaughtFish");
    L.table(2);
    L.str("Fish_Tartarus_Common_01"); L.num(3);
    L.str("Fish_Asphodel_Common_01"); L.num(0);
    L.str("Flags");
    L.table(4);
    L.num(1); L.str("one");                    // number keys
    L.num(2.5); L.str("two and a half");
    L.boolean(true); L.nil();                  // boolean key, nil value
    L.table(1); L.str("x"); L.num(1); L.num(7);   // a table as a key: named ""
    L.str("Filler");
    L.table((uint32_t)filler);
    for (int i = 0; i < filler; i++) {
        L.str("Room_" + std::to_string(i % 97) + "_" + std::to_string(i));
        L.table(3);
        L.str("Name"); L.str("Some room description that repeats a lot " + std::to_string(i % 13));
        L.str("Count"); L.num(i);
        L.str("Nested"); L.table(1); L.str("Deep"); L.num(i * 0.5);
    }
    L.str("TextLinesRecord");
    L.table(3);
    L.str("CerberusGift01"); L.boolean(true);
    L.str("MegaeraGift01"); L.boolean(true);
    L.str("NyxGift09"); L.boolean(true);
    L.str("CurrentRun");
    L.table(1); L.str("Hero"); L.table(1); L.str("Health"); L.num(50);
    L.str("CodexStatus"); L.table(0);
    L.str("Unrelated"); L.str(std::string(70000, 'z'));   // a long string to skip, bigger than the window
    L.str("ConfigOptionCache"); L.table(1); L.str("Volume"); L.num(0.8);
    L.num(99);                                  // a later global, dropped by both readers
    return L.b;
}

// ---------------------------------------------------------------- tree comparison
static std::string dump(const cJSON* n) {
    char* s = cJSON_PrintUnformatted(n);
    std::string r = s ? s : "";
    cJSON_free(s);
    return r;
}

// In-memory save source: "app" (whole files) or "module" (pieces, small heap).
struct MemSource : SaveSource {
    std::map<std::string, std::vector<uint8_t>> files;
    bool stream = false;
    size_t mem = (size_t)-1;
    size_t maxRead = 0;   // largest single read, to check the module never asks for a whole file
    bool open(uint64_t) override { return true; }
    void close() override {}
    std::string lastError() override { return ""; }
    bool readAll(const std::string& rel, std::vector<uint8_t>& out) override {
        auto it = files.find(rel);
        if (it == files.end()) return false;
        if (stream && it->second.size() + 64 * 1024 > mem) return false;
        out = it->second;
        if (out.size() > maxRead) maxRead = out.size();
        return true;
    }
    bool streaming() const override { return stream; }
    bool fileSize(const std::string& rel, uint64_t& size) override {
        auto it = files.find(rel);
        if (it == files.end()) return false;
        size = it->second.size();
        return true;
    }
    bool readAt(const std::string& rel, uint64_t off, void* buf, size_t len) override {
        auto it = files.find(rel);
        if (it == files.end() || off + len > it->second.size()) return false;
        memcpy(buf, it->second.data() + off, len);
        if (len > maxRead) maxRead = len;
        return true;
    }
    size_t memoryAvailable() override { return mem; }
};

static ReadAtFn memReader(const std::vector<uint8_t>& f) {
    return [&f](uint64_t off, void* buf, size_t len) {
        if (off + len > f.size()) return false;
        memcpy(buf, f.data() + off, len);
        return true;
    };
}

int main(int argc, char** argv) {
    const char* defDir = argc > 1 ? argv[1] : "romfs/games";

    // --- the LZ4 compressor really makes matches, and the app's decoder accepts it
    std::vector<uint8_t> state = hadesState(true, 6000, false);
    std::vector<uint8_t> z = lz4Compress(state), back;
    ok(lz4BlockDecompress(z.data(), z.size(), back, 1 << 26) && back == state, "test LZ4 compressor round trip");
    ok(z.size() * 3 < state.size(), "it compresses (matches used)");
    printf("     state %zu KB, compressed %zu KB\n", state.size() >> 10, z.size() >> 10);

    // --- whole tree (no paths filter = empty trie keeps nothing but _header; use a root path to keep everything)
    std::vector<uint8_t> file = sgbFile(state, 42);
    std::string err;
    cJSON* full = sgbToJson(file.data(), file.size(), err);
    ok(full != nullptr, "app reader parses the synthetic save");
    std::vector<std::string> everything = {"GameState", "TextLinesRecord", "CurrentRun", "CodexStatus", "Unrelated", "ConfigOptionCache"};
    cJSON* all = sgbToJsonStream(memReader(file), file.size(), everything, 0, err);
    ok(all != nullptr, "streamed reader parses it");
    ok(full && all && dump(full) == dump(all), "every global kept: streamed tree == app tree, byte for byte");
    if (all) cJSON_Delete(all);

    // --- pruned: only what the paths reach, values identical
    std::vector<std::string> some = {"GameState/RoomCountCache/B_Intro", "GameState/QuestStatus[*]", "GameState/WeaponUnlocks[*][*]",
                                     "GameState/Flags/1", "GameState/Flags/2.5", "GameState/Flags/true", "GameState/Flags/",
                                     "GameState/RoomCountCache/RoomOpeningWeird", "TextLinesRecord/NyxGift09", "GameState/Filler/Room_5_5/Nested"};
    cJSON* pr = sgbToJsonStream(memReader(file), file.size(), some, 0, err);
    ok(pr != nullptr, "pruned parse");
    if (pr && full) {
        auto get = [](cJSON* root, const char* path) { return dump(cJSON_GetObjectItemCaseSensitive(root, path)); };
        cJSON* gsF = cJSON_GetObjectItemCaseSensitive(full, "GameState");
        cJSON* gsP = cJSON_GetObjectItemCaseSensitive(pr, "GameState");
        ok(gsP && get(gsF, "QuestStatus") == get(gsP, "QuestStatus") && get(gsF, "WeaponUnlocks") == get(gsP, "WeaponUnlocks"),
           "[*] paths: whole subtables identical");
        cJSON* rcF = cJSON_GetObjectItemCaseSensitive(gsF, "RoomCountCache");
        cJSON* rcP = cJSON_GetObjectItemCaseSensitive(gsP, "RoomCountCache");
        ok(rcP && cJSON_GetArraySize(rcP) == 2 && get(rcF, "B_Intro") == get(rcP, "B_Intro") &&
               get(rcF, "RoomOpeningWeird") == get(rcP, "RoomOpeningWeird") && !cJSON_GetObjectItemCaseSensitive(rcP, "A_Intro"),
           "only the asked keys kept (NUL-cut key matches the app's name)");
        cJSON* flP = cJSON_GetObjectItemCaseSensitive(gsP, "Flags");
        ok(flP && cJSON_GetArraySize(flP) == 4 && get(cJSON_GetObjectItemCaseSensitive(gsF, "Flags"), "2.5") == get(flP, "2.5"),
           "number, boolean and table keys named like the app");
        cJSON* filP = cJSON_GetObjectItemCaseSensitive(gsP, "Filler");
        ok(filP && cJSON_GetArraySize(filP) == 1 &&
               get(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(gsF, "Filler"), "Room_5_5"), "Nested") ==
                   get(cJSON_GetObjectItemCaseSensitive(filP, "Room_5_5"), "Nested"),
           "deep path through 6000 skipped rooms");
        ok(!cJSON_GetObjectItemCaseSensitive(pr, "CurrentRun") && !cJSON_GetObjectItemCaseSensitive(pr, "Unrelated"), "the rest skipped");
        cJSON* hdF = cJSON_GetObjectItemCaseSensitive(full, "_header");
        cJSON* hdP = cJSON_GetObjectItemCaseSensitive(pr, "_header");
        ok(hdP && dump(hdF) == dump(hdP), "_header always built, identical");
        ok(dump(pr).size() * 50 < dump(full).size(), "pruned tree is tiny compared with the full one");
    }
    if (pr) cJSON_Delete(pr);
    if (full) cJSON_Delete(full);

    // --- a non-table global first: both readers skip it and take the next table
    std::vector<uint8_t> f2 = sgbFile(hadesState(false, 10, true), 3);
    cJSON* a = sgbToJson(f2.data(), f2.size(), err);
    cJSON* b = sgbToJsonStream(memReader(f2), f2.size(), everything, 0, err);
    ok(a && b && dump(a) == dump(b), "number before the root table: same tree");
    if (a) cJSON_Delete(a);
    if (b) cJSON_Delete(b);

    // --- errors are clean (no crash, no huge allocation)
    std::vector<uint8_t> cut(file.begin(), file.begin() + file.size() / 2);
    ok(!sgbToJsonStream(memReader(cut), cut.size(), some, 0, err), "truncated file rejected");
    printf("     (%s)\n", err.c_str());
    std::vector<uint8_t> bad = file;
    for (size_t i = 200; i < 260 && i < bad.size(); i++) bad[i] ^= 0x5A;
    cJSON* t = sgbToJsonStream(memReader(bad), bad.size(), some, 0, err);
    ok(true, "corrupted data does not crash");
    if (t) cJSON_Delete(t);
    ok(!sgbToJsonStream(memReader(file), file.size(), everything, 200 * 1024, err) && err.find("not enough memory") != std::string::npos,
       "budget too small for the asked values: clean 'not enough memory'");
    std::vector<uint8_t> notSgb(100, 0);
    ok(!sgbToJsonStream(memReader(notSgb), notSgb.size(), some, 0, err), "not an SGB file");

    // --- the real Hades definition through the rule engine: module path == app path, for every achievement
    std::string path = std::string(defDir) + "/0100535012974000.json";
    std::vector<uint8_t> defText;
    ok(readFile(path, defText), "Hades definition found");
    defText.push_back(0);
    GameDef def;
    std::string derr;
    ok(parseGameDef((const char*)defText.data(), def, derr), "Hades definition parses");
    for (int variant = 0; variant < 2; variant++) {
        bool cleared = variant == 1;
        std::vector<uint8_t> save = sgbFile(hadesState(cleared, 30000, false), 120);   // ~3 MB of Lua state
        Game app, mod;
        app.id = mod.id = def.id;
        applyGameDef(app, def);
        applyGameDef(mod, def);
        MemSource sa, sm;
        sa.files["Saved Games/Hades/Profile1.sav"] = save;
        sm.files["Saved Games/Hades/Profile1.sav"] = save;
        sm.stream = true;
        sm.mem = 1536 * 1024;   // the live module on a busy console: 1.5 MB free
        evaluateSave(app, sa);
        evaluateSave(mod, sm);
        bool same = app.saveStatus == SaveStatus::Ok && mod.saveStatus == SaveStatus::Ok && app.achs.size() == mod.achs.size();
        int compared = 0;
        for (size_t i = 0; same && i < app.achs.size(); i++) {
            if (!isSaveRule(app.achs[i].rule.type)) continue;
            compared++;
            if (std::fabs(app.achs[i].value - mod.achs[i].value) > 1e-9) {
                printf("     %s: app %g, module %g\n", app.achs[i].id.c_str(), app.achs[i].value, mod.achs[i].value);
                same = false;
            }
        }
        char msg[160];
        snprintf(msg, sizeof(msg), "%s: all %d Hades save rules give the app's values in the module (1.5 MB heap, %zu KB save)",
                 cleared ? "Tartarus cleared" : "not cleared", compared, save.size() >> 10);
        ok(same && compared > 30, msg);
        if (!same) for (const std::string& l : mod.debug) printf("     | %s\n", l.c_str());
        double tart = -1;
        for (const Achievement& x : mod.achs) if (x.id == "escaped_tartarus") tart = x.value;
        ok(tart == (cleared ? 1 : 0), cleared ? "Escaped Tartarus met (module)" : "Escaped Tartarus not met before Asphodel (module)");
        ok(sm.maxRead <= 16384, "module read the save 16 KB at a time, never whole");
    }

    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
