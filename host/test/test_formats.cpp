// g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_formats.cpp source/save_formats.cpp source/json_doc.cpp source/byml.cpp -x c source/cJSON.c -o /tmp/test_formats
// /tmp/test_formats [--dump OUTDIR] [--key PASSWORD_FILE] [files...]
// Checks the "gvas" and "nrbf" readers on the Save Lab fixtures (tools/savelab/test/fixtures). With --dump, writes
// each file's tree as OUTDIR/<file name>.json so that tools/savelab/test/test_formats.js can compare it with the
// Save Lab's tree (CPP_DUMP=OUTDIR node tools/savelab/test/test_formats.js).
// THEATRHYTHM saves (.inf) need the password: --key FILE (default: the fixtures' dummy one; the Save Lab side
// takes the same file from THEATRHYTHM_KEY).
#include "save_formats.h"
#include "cJSON.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

static int fails = 0;
static void check(bool c, const char* m) { printf("%s %s\n", c ? "ok  " : "FAIL", m); if (!c) fails++; }

static std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
template <class Doc> static Doc load(const std::string& p) { Doc d; d.parse(readFile(p)); return d; }
template <class Doc> static std::vector<BymlValue> q(const Doc& d, const char* p) { std::vector<BymlValue> v; d.query(p, v); return v; }
template <class Doc> static double one(const Doc& d, const char* p) { auto v = q(d, p); return v.size() == 1 ? v[0].num : -12345; }

int main(int argc, char** argv) {
    std::string dump, keyFile = "tools/savelab/test/fixtures/theatrhythm/password.txt";
    std::vector<std::string> files;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "--key") && i + 1 < argc) keyFile = argv[++i];
        else files.push_back(argv[i]);
    }
    const std::string fx = "tools/savelab/test/fixtures/";
    if (files.empty()) {
        // ---- GVAS
        GvasDoc s = load<GvasDoc>(fx + "gvas/Slot1.sav");
        check(s.error.empty(), "Slot1.sav parses");
        check(one(s, "u8_test") == 129 && one(s, "i8_test") == -123 && one(s, "int32_test") == -2147483647, "integer properties");
        check(one(s, "f_property") == 3.14159 && one(s, "d_property") == 3.14159265358979, "float (6 decimals) / double");
        auto v = q(s, "str_property");
        check(v.size() == 1 && v[0].kind == BymlValue::String && v[0].str == "Hello world", "string property");
        check(one(s, "struct_property/test_field") == 12345, "custom struct");
        check(q(s, "array_of_structs[*]/test_field").size() == 2 && one(s, "array_of_ints[4]") == 12, "arrays of structs / ints");
        check(one(s, "date_time_property") == 637864237380020000.0, "DateTime as ticks");
        GvasDoc a = load<GvasDoc>(fx + "gvas/assert_failed.sav");
        check(one(a, "Achievements/Ironman_95_5DB8DC864790570EF83371B36654A0D2") == 1, "bool in nested struct");
        check(q(a, "Achievements/BooksCollected_91_CA78AEE9434D1D051FE547B257A574FB[=E_Books::NewEnumerator0]").size() == 1, "enum array membership");
        v = q(a, "AreaIdentifier (FurthestProgress)");
        check(v.size() == 1 && v[0].num == 2, "property names with spaces");
        GvasDoc t = load<GvasDoc>(fx + "gvas/tagcontainer.sav");
        check(q(t, "TagTest[=Character.Appearance.OverallColor.Gold]").size() == 1, "GameplayTagContainer membership");
        GvasDoc r = load<GvasDoc>(fx + "gvas/regression_01.bin");
        v = q(r, "Thing");
        check(v.size() == 1 && v[0].str == "B38299D449D53DDFC757AEB4E438581D", "Guid as hex");
        GvasDoc vd = load<GvasDoc>(fx + "gvas/vector2d.sav");
        check(vd.error.empty() && vd.info.skipped == 0 && q(vd, "SettingsChanged").size() == 1, "UE 5.3 file, delegates read as null");
        GvasDoc u54 = load<GvasDoc>(fx + "gvas/ue54_checkpoint.sav");
        check(u54.error.empty() && u54.info.skipped == 0 && one(u54, "Checkpoint") == 36 && q(u54, "TrophyDataWrapper[*]/Collected_17_531EBC9943642622D65F1BB1ECB9B21E").size() == 9,
              "UE 5.4 file (complete type names, struct array without inner tag)");
        {
            v = q(s, "u8_test");
            std::vector<BymlValue> n;
            GvasDoc g2 = load<GvasDoc>(fx + "gvas/Options.sav");
            check(one(g2, "Slider1") == 0.166107, "float rounding");
        }
        GvasDoc bad;
        std::vector<uint8_t> junk = readFile(fx + "gvas/Slot1.sav");
        junk.resize(200);
        check(!bad.parse(std::move(junk)) && !bad.error.empty(), "truncated file rejected");
        // ---- NRBF
        NrbfDoc n0 = load<NrbfDoc>(fx + "nrbf/save0.dat"), n1 = load<NrbfDoc>(fx + "nrbf/save1.dat");
        check(n0.error.empty() && n1.error.empty(), "NRBF saves parse");
        check(one(n0, "level") == 1 && one(n1, "level") == 12 && one(n1, "beatBoss") == 1, "class fields");
        check(one(n1, "stats/kills") == 250 && one(n1, "stats/distance") == 2.5 && one(n1, "statsAgain/kills") == 250, "nested class + shared reference");
        check(one(n1, "difficulty") == 7, "enum as number");
        check(q(n0, "collected[*]").size() == 1 && q(n1, "collected[*]").size() == 3 && one(n1, "collected[2]") == 8, "List<int> trimmed to _size");
        check(one(n0, "flags[Key=Ending_A]/Value") == 0 && one(n1, "flags[Key=Ending_A]/Value") == 1, "Dictionary<string,bool>");
        check(one(n1, "perLevel[Key=2]/Value/kills") == 9 && q(n0, "perLevel[Key=2]/Value/kills").empty(), "Dictionary<int,class>");
        check(q(n1, "unlocked[=Bow]").size() == 1 && q(n0, "unlocked[=Bow]").empty(), "HashSet<string>");
        check(q(n0, "mixed[*]").size() == 300 && one(n0, "mixed[0]") == 42 && one(n0, "mixed[5]/kills") == 0, "object[] with null runs");
        check(one(n1, "Coins") == 77, "auto-property backing field renamed");
        check(one(n0, "grid[5]") == 6 && one(n0, "gold") == 123.45 && one(n1, "played") == 144000000000.0, "2-D array, decimal, TimeSpan");
        v = q(n0, "loop/next/next");
        check(v.size() == 1 && v[0].kind == BymlValue::Null, "reference cycle cut");
        v = q(n0, "grade");
        check(v.size() == 1 && v[0].str == "\xC3\xA9", "char");
        NrbfDoc ns = load<NrbfDoc>(fx + "nrbf/string.dat");
        v = q(ns, "value");
        check(v.size() == 1 && v[0].str == "base64-payload==", "string root wrapped in {value}");
        NrbfDoc nl = load<NrbfDoc>(fx + "nrbf/list.dat");
        check(q(nl, "[=y]").size() == 1 && q(nl, "[*]").size() == 2, "list at the root");
        {   // strings are not converted to numbers (unlike JSON text saves)
            GvasDoc p = load<GvasDoc>(fx + "gvas/assert_failed.sav");
            v = q(p, "BrambleGameSettings/Language_77_80BF4E304912ECD8C1EAEB8C6A07EC36");
            check(v.size() == 1 && v[0].kind == BymlValue::String, "GVAS string stays a string");
        }
        // ---- RPG Maker MV (LZString) / MZ (zlib written as UTF-8), JsonEx markers removed
        for (const char* f : {"rpgmaker/file1.rpgsave", "rpgmaker/file1.rmmzsave"}) {
            RpgMakerDoc r = load<RpgMakerDoc>(fx + f);
            check(r.error.empty(), (std::string(f) + " parses").c_str());
            check(one(r, "switches/_data[3]") == 1 && one(r, "switches/_data[4]") == 0 && one(r, "variables/_data[5]") == 42, "switches / variables");
            check(one(r, "party/_gold") == 9876 && one(r, "party/_items/12") == 1 && one(r, "actors/_data[1]/_level") == 17, "party, items, actors");
            check(one(r, "system/_battleCount") == 12 && one(r, "selfSwitches/_data/1,4,A") == 1, "system, self switches");
            check(q(r, "system/@").empty() && q(r, "system/@c").empty() && q(r, "party/_actors[*]").size() == 1, "JsonEx markers removed");
            auto lv = q(r, "party/_leader");
            check(lv.size() == 1 && lv[0].kind == BymlValue::Null, "@r reference -> null");
        }
        {
            RpgMakerDoc g = load<RpgMakerDoc>(fx + "rpgmaker/global.rpgsave");
            auto t = q(g, "[1]/title");
            check(t.size() == 1 && t[0].str == "Test", "global.rpgsave (list at the root)");
            std::vector<uint8_t> j = readFile(fx + "gvas/Slot1.sav");
            check(!isRpgMaker(j.data(), j.size()), "GVAS is not RPG Maker");
            std::string plain = "{\"name\": 1}";
            check(!isRpgMaker((const uint8_t*)plain.data(), plain.size()), "plain JSON without RPG Maker keys is left to the json reader");
        }
        // ---- GameMaker INI / base64 INI / lines, ds_map / ds_list hex
        for (const char* f : {"gamemaker/save.ini", "gamemaker/save_b64.sav"}) {
            GameMakerDoc m = load<GameMakerDoc>(fx + f);
            check(m.error.empty(), (std::string(f) + " parses").c_str());
            check(one(m, "Progress/Level") == 12 && one(m, "Progress/Chapter") == 3 && one(m, "10/Best") == 1000, "INI numbers");
            auto nm = q(m, "Progress/Name");
            check(nm.size() == 1 && nm[0].str == "Frisk", "INI string unquoted");
            check(one(m, "Stats/Deaths") == 5 && one(m, "Stats/Kills") == 0, "repeated key keeps the last value, spaces around =");
            check(one(m, "Progress/Flags/coins") == 300 && one(m, "Progress/Flags/boss_dead") == 1 && one(m, "Progress/Flags/1") == -7, "ds_map hex");
            check(one(m, "Progress/Flags/nested[2]") == 3 && q(m, "Progress/Flags/nested[*]").size() == 3, "nested ds_list hex");
        }
        {
            GameMakerDoc l = load<GameMakerDoc>(fx + "gamemaker/file0");
            auto it = q(l, "lines[2][4]");
            check(one(l, "lines[1]") == 120 && it.size() == 1 && it[0].str == "item" && one(l, "lines[2][2]") == 1 && one(l, "lines[3]") == -3.5, "line file + ds_list");
            std::vector<uint8_t> a = readFile(fx + "gamemaker/save.ini"), b = readFile(fx + "gamemaker/save_b64.sav"), c = readFile(fx + "gamemaker/file0");
            std::vector<uint8_t> js = readFile(fx + "rpgmaker/file1.rpgsave");
            check(isGameMaker(a.data(), a.size()) && isGameMaker(b.data(), b.size()) && isGameMaker(c.data(), c.size()), "GameMaker files detected");
            std::string json = "{\"a\": 1}\n", txt = "hello\nworld\n";
            check(!isGameMaker((const uint8_t*)json.data(), json.size()) && !isGameMaker((const uint8_t*)txt.data(), txt.size()), "JSON / plain text not taken");
        }
        const char* all[] = {"gvas/Slot1.sav", "gvas/assert_failed.sav", "gvas/tagcontainer.sav", "gvas/regression_01.bin", "gvas/vector2d.sav",
                             "gvas/Options.sav", "gvas/ue54_checkpoint.sav", "nrbf/save0.dat", "nrbf/save1.dat", "nrbf/string.dat", "nrbf/list.dat",
                             "theatrhythm/backup.inf", "theatrhythm/musicBackup.inf", "theatrhythm/proficaBackup.inf", "theatrhythm/edge.inf",
                             "rpgmaker/file1.rpgsave", "rpgmaker/file1.rmmzsave", "rpgmaker/global.rpgsave",
                             "gamemaker/save.ini", "gamemaker/save_b64.sav", "gamemaker/file0"};
        for (const char* f : all) files.push_back(fx + f);
        if (dump.empty()) files.clear();
    }
    std::string password;
    {
        std::vector<uint8_t> k = readFile(keyFile);
        password.assign(k.begin(), k.end());
        while (!password.empty() && (password.back() == '\n' || password.back() == '\r' || password.back() == ' ')) password.pop_back();
    }
    for (const std::string& f : files) {
        std::vector<uint8_t> d = readFile(f);
        std::string err;
        SaveFormatInfo info;
        cJSON* t = isGvas(d.data(), d.size()) ? gvasToJson(d.data(), d.size(), err, &info)
                 : isNrbf(d.data(), d.size()) ? nrbfToJson(d.data(), d.size(), err, &info)
                 : isRpgMaker(d.data(), d.size()) ? rpgMakerToJson(d.data(), d.size(), err, &info)
                 : isGameMaker(d.data(), d.size()) ? gameMakerToJson(d.data(), d.size(), err, &info)
                 : isTheatrhythm(d.data(), d.size()) ? theatrhythmToJson(d.data(), d.size(), password, err, &info) : nullptr;
        std::string name = f.substr(f.find_last_of('/') + 1);
        printf("     %s: %s\n", name.c_str(), t ? (info.summary + (info.skipped ? ", " + std::to_string(info.skipped) + " skipped" : "")).c_str() : ("ERROR " + err).c_str());
        if (t && !dump.empty()) {
            char* s = cJSON_PrintUnformatted(t);
            std::ofstream o(dump + "/" + name + ".json", std::ios::binary);
            o << s;
            free(s);
        }
        if (t) cJSON_Delete(t);
    }
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
