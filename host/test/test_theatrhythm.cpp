// g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_theatrhythm.cpp source/rules.cpp source/fileutil.cpp source/save_formats.cpp source/json_doc.cpp source/byml.cpp -x c source/cJSON.c -o /tmp/test_theatrhythm
// /tmp/test_theatrhythm            (run from the repository root)
// "theatrhythm" reader (AES-128-CBC + raw DEFLATE + MessagePack) and generic MessagePack / inflate, on the synthetic
// saves of tools/savelab/test/fixtures/theatrhythm (DUMMY password, made by make_theatrhythm_fixtures.js; the
// Save Lab checks the same files in test_formats.js). Also the definition romfs/games/010081B01777C000.json through
// evaluateSave, and the "password file missing" path. Clean under -fsanitize=address,undefined.
#include "rules.h"
#include "save_formats.h"
#include "cJSON.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>

static int fails = 0;
static void check(bool c, const char* m) { printf("%s %s\n", c ? "ok  " : "FAIL", m); if (!c) fails++; }
static std::vector<uint8_t> slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static const std::string FX = "tools/savelab/test/fixtures/theatrhythm/";
static std::string password() {
    std::vector<uint8_t> b = slurp(FX + "password.txt");
    std::string s(b.begin(), b.end());
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}
static TheatrhythmDoc load(const char* f, const std::string& pw) { TheatrhythmDoc d; d.password = pw; d.parse(slurp(FX + f)); return d; }
static std::vector<BymlValue> q(const JsonDoc& d, const char* p) { std::vector<BymlValue> v; d.query(p, v); return v; }
static double one(const JsonDoc& d, const char* p) { auto v = q(d, p); return v.size() == 1 ? v[0].num : -12345; }
static std::string str1(const JsonDoc& d, const char* p) { auto v = q(d, p); return v.size() == 1 ? v[0].str : "<none>"; }

// Save source over a folder (the fixtures)
class DirSource : public SaveSource {
public:
    std::string dir;
    bool open(uint64_t) override { return true; }
    void close() override {}
    std::string lastError() override { return ""; }
    bool readAll(const std::string& rel, std::vector<uint8_t>& out) override {
        std::ifstream f(dir + rel, std::ios::binary);
        if (!f) return false;
        out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return true;
    }
};

int main() {
    const std::string pw = password();
    check(!pw.empty(), "dummy password of the fixtures");

    // ---- raw inflate
    {
        const uint8_t fixed[] = {0x2b, 0xce, 0xc8, 0x2f, 0x2a, 0x51, 0x28, 0x49, 0xad, 0x28, 0x51, 0x28, 0x86, 0x33, 0x01};   // fixed Huffman
        std::vector<uint8_t> out;
        check(inflateRaw(fixed, sizeof fixed, out, 1 << 20) && std::string(out.begin(), out.end()) == "short text short text", "inflate: fixed Huffman block");
        const uint8_t stored[] = {0x01, 0x03, 0x00, 0xfc, 0xff, 'a', 'b', 'c'};
        check(inflateRaw(stored, sizeof stored, out, 1 << 20) && std::string(out.begin(), out.end()) == "abc", "inflate: stored block");
        std::string e;
        const uint8_t badType[] = {0x07};
        check(!inflateRaw(badType, 1, out, 1 << 20, &e) && e.find("block type") != std::string::npos, "inflate: bad block type");
        check(!inflateRaw(fixed, 6, out, 1 << 20, &e) && !e.empty(), "inflate: truncated input");
        check(!inflateRaw(fixed, sizeof fixed, out, 5, &e) && e.find("too large") != std::string::npos, "inflate: output limit");
        const uint8_t far[] = {0x03, 0x02, 0x00};   // fixed block: length 3 at distance 1 with nothing written yet
        check(!inflateRaw(far, sizeof far, out, 1 << 20, &e), "inflate: distance too far back rejected");
    }
    // ---- MessagePack
    {
        std::string e;
        const uint8_t m1[] = {0x83, 0xa1, 'b', 0x01, 0x05, 0xa1, 'x', 0xa1, 'b', 0x02};   // {"b":1, 5:"x", "b":2}
        cJSON* t = msgpackToJson(m1, sizeof m1, e);
        char* s = t ? cJSON_PrintUnformatted(t) : nullptr;
        check(s && !strcmp(s, "{\"5\":\"x\",\"b\":2}"), "MessagePack: integer keys first, repeated key keeps its place");
        free(s);
        cJSON_Delete(t);
        const uint8_t huge[] = {0xdd, 0xff, 0xff, 0xff, 0xff};
        check(!msgpackToJson(huge, sizeof huge, e) && e.find("bad length") != std::string::npos, "MessagePack: absurd length rejected");
        std::vector<uint8_t> deep(100, 0x91);
        deep.push_back(0x01);
        check(!msgpackToJson(deep.data(), deep.size(), e) && e.find("deep") != std::string::npos, "MessagePack: nesting limit");
        const uint8_t inv[] = {0xc1};
        check(!msgpackToJson(inv, 1, e) && e.find("invalid byte") != std::string::npos, "MessagePack: 0xC1 rejected");
        for (size_t cut = 1; cut < sizeof m1; cut++) {
            cJSON* x = msgpackToJson(m1, cut, e);
            if (x) { check(false, "MessagePack: truncated map accepted"); cJSON_Delete(x); break; }
        }
    }
    // ---- THEATRHYTHM container
    {
        std::vector<uint8_t> b = slurp(FX + "backup.inf");
        check(isTheatrhythm(b.data(), b.size()) && !isTheatrhythm(b.data(), b.size() - 1), "size check (header + AES blocks)");
        std::string e;
        check(!theatrhythmToJson(b.data(), b.size(), "", e) && e == "no password", "no password: not read");
        check(!theatrhythmToJson(b.data(), b.size(), "wrong password", e) && e.find("wrong password") != std::string::npos, "wrong password: clear error");
        std::vector<uint8_t> dam = b;
        dam[100] ^= 0x55;
        cJSON* t = theatrhythmToJson(dam.data(), dam.size(), pw, e);
        check(!t, "damaged ciphertext rejected");
        cJSON_Delete(t);
    }
    TheatrhythmDoc bk = load("backup.inf", pw), mu = load("musicBackup.inf", pw), pr = load("proficaBackup.inf", pw), ed = load("edge.inf", pw);
    check(bk.error.empty() && mu.error.empty() && pr.error.empty() && ed.error.empty(), "the four fixtures decode");
    check(bk.info.summary.find("Switch") != std::string::npos && mu.info.summary.find("PS4") != std::string::npos && bk.info.summary.find("v1.00") != std::string::npos,
          "Switch and PS4 layouts, version in the summary");
    check(one(bk, "MusicStageInfo/TotalScore") == 5000000123.0 && one(bk, "RhythpoInfo/TotalRhythpoValue") == 123456 && one(bk, "UserInfo/PlayTimeDouble") == 3600.25,
          "64-bit int, int, double");
    check(one(bk, "MusicSelectInfo/PickupLastLottedDateUniversal") == 1795262771 && one(bk, "RhythpoInfo/EverydayPlayBonus/LastDateTime") == -62135596800.0, "timestamps -> seconds");
    check(one(bk, "SeriesQuestInfo/WorldDictionary/50000003/Cleared") == 1 && q(bk, "SeriesQuestInfo/WorldDictionary[*]/Completed").size() == 13, "int-keyed Dictionary -> object");
    check(one(mu, "MusicInfo/MusicRecordDictionary/160000005/DifficultyRecordDictionary/1/PlayStyleRecordDictionary/0/TotalPlayCount") == 11 &&
          one(mu, "MusicInfo/MusicRecordDictionary/160000005/DifficultyRecordDictionary/1/PlayStyleRecordDictionary/0/CriticalBarZones[1]") == 0.05 &&
          q(mu, "MusicInfo/MusicRecordDictionary[*]/DifficultyRecordDictionary[*]/PlayStyleRecordDictionary[*]/TotalFullChainClearCount").size() == 27,
          "PlayStyleRecord fields labelled");
    check(one(pr, "ProficaInfo/MyProfica/ProficaIllustID") == 150000031 && str1(pr, "ProficaInfo/MyProfica/MyGuid") == "2f911e53-c980-40ff-8394-4047f5b8cf27", "MyProfica labelled");
    check(str1(ed, "4294967295") == "max u32 key" && str1(ed, "12345678901234567890") == "u64 key" && str1(ed, "-1") == "neg key" && str1(ed, "null") == "nil key" &&
          str1(ed, "<float>") == "float key" && str1(ed, "<map>") == "map key" && str1(ed, "<ext>") == "ext key", "MessagePack keys of every type");
    check(str1(ed, "nul") == "a" && str1(ed, "bin") == "0001ABFF" && one(ed, "f32") == 0.1 && q(ed, "nan")[0].kind == BymlValue::Null && one(ed, "ts8") == 1700000001 &&
          str1(ed, "ext") == "ext:7:DEAD" && one(ed, "dup") == 2 && one(ed, "__proto__/polluted") == 1, "MessagePack values: NUL, bin, float, non-finite, ext, duplicates");

    // ---- the definition, through evaluateSave (app path)
    {
        std::vector<uint8_t> text = slurp("romfs/games/010081B01777C000.json");
        text.push_back(0);
        GameDef def;
        std::string err;
        check(parseGameDef((const char*)text.data(), def, err) && err.empty() && def.reader == "theatrhythm" && def.keyFile == "theatrhythm.txt" && !def.closeScan,
              "definition: reader, key_file, close_scan false");
        Game g;
        applyGameDef(g, def);
        DirSource src;
        src.dir = FX;
        setSaveKeyDir("");
        evaluateSave(g, src);
        bool said = false;
        for (const std::string& l : g.debug) if (l.find("theatrhythm.txt") != std::string::npos && l.find("missing") != std::string::npos) said = true;
        check(g.saveStatus == SaveStatus::Unreadable && said, "no password file: save skipped with a clear message");
        Game h;
        applyGameDef(h, def);
        h.keyFile = "password.txt";   // the fixtures' dummy password
        setSaveKeyDir(FX.substr(0, FX.size() - 1));
        evaluateSave(h, src);
        std::map<std::string, bool> met;
        int missing = 0;
        for (const Achievement& a : h.achs) if (isSaveRule(a.rule.type)) met[a.id] = a.value + 1e-9 >= a.rule.target;
        for (const std::string& l : h.debug) if (l.find("not found") != std::string::npos) { missing++; printf("     %s\n", l.c_str()); }
        check(h.saveStatus == SaveStatus::Ok && missing == 0, "both files read, every rule path found");
        const char* want[] = {"first_perfect_chain", "all_critical", "ending", "played_300", "encore", "chains_100", "points_5b", "items_100", "abilities_1000",
                              "enemies_5000", "lv99_x4", "star9", "rhythmia_100k", "cards_300", "drops_1000", "summonstones_30", "quests_10",
                              "quest_complete_10", "endless_20", "top_hits_5"};
        bool all = true;
        for (const char* w : want) if (!met[w]) { all = false; printf("     not met: %s\n", w); }
        check(all && !met["cards_1000"] && !met["quest_complete_20"] && !met["quest_complete_all"], "THEATRHYTHM rules (same results as the Save Lab)");
        setSaveKeyDir("");
    }
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
