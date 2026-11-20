// "deltarune" reader (DELTARUNE deltarune.sav) on the synthetic saves of tools/deltarune/make_test_save.py.
//   python3 tools/deltarune/make_test_save.py /tmp/dr
//   g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_deltarune.cpp source/save_formats.cpp source/json_doc.cpp source/byml.cpp -x c source/cJSON.c -o /tmp/test_deltarune
//   /tmp/test_deltarune /tmp/dr [--dump OUTDIR]
// --dump writes each tree as OUTDIR/<file>.json for the Save Lab cross-check
// (CPP_DUMP=OUTDIR DELTARUNE_SAVES=/tmp/dr node tools/savelab/test/test_formats.js).
#include "save_formats.h"
#include "cJSON.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static std::vector<BymlValue> q(const DeltaruneDoc& d, const char* p) { std::vector<BymlValue> v; d.query(p, v); return v; }
static double one(const DeltaruneDoc& d, const char* p) { auto v = q(d, p); return v.size() == 1 ? v[0].num : -12345; }
static std::string str(const DeltaruneDoc& d, const char* p) { auto v = q(d, p); return v.size() == 1 && v[0].kind == BymlValue::String ? v[0].str : "?"; }
static std::string ones(const DeltaruneDoc& d, const char* p) {   // indexes of the non-zero values of a list
    std::string s;
    auto v = q(d, p);
    for (size_t i = 0; i < v.size(); i++) if (v[i].num != 0) s += (s.empty() ? "" : ",") + std::to_string(i);
    return s;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: test_deltarune <dir of make_test_save.py> [--dump OUTDIR]\n"); return 2; }
    std::string dir = argv[1], dump = argc > 3 && !strcmp(argv[2], "--dump") ? argv[3] : "";
    std::vector<uint8_t> raw = readFile(dir + "/deltarune.sav");
    CHECK(!raw.empty() && isDeltarune(raw.data(), raw.size()));
    DeltaruneDoc d;
    std::vector<uint8_t> copy(raw);
    CHECK(d.parse(std::move(copy)));
    printf("%s\n", d.info.summary.c_str());
    // files, sorted by name
    CHECK(q(d, "files[*]").size() == 5);
    CHECK(str(d, "files[0]/name") == "filech1_0" && one(d, "files[0]/chapter") == 1 && one(d, "files[0]/slot") == 0);
    CHECK(one(d, "files[name=filech1_3]/completion") == 1 && one(d, "files[name=filech1_3]/slot") == 0);
    CHECK(str(d, "files[name=filech5_0_b]/route") == "_b" && one(d, "files[name=filech5_0_b]/chapter") == 5);
    // chapter 1 layout (9999 flags, 4-member lists, 13 weapons)
    CHECK(str(d, "files[0]/truename") == "Kris");
    CHECK(one(d, "files[0]/gold") == 30 && one(d, "files[0]/plot") == 40 && one(d, "files[0]/room") == 10 && one(d, "files[0]/time") == 9000);
    CHECK(q(d, "files[0]/char[*]").size() == 3 && one(d, "files[0]/char[1]") == 2);
    CHECK(q(d, "files[0]/chararmor1[*]").size() == 4 && one(d, "files[0]/chararmor1[1]") == 1);
    CHECK(q(d, "files[0]/weapon[*]").size() == 13 && q(d, "files[0]/pocketitem").empty());
    CHECK(one(d, "files[0]/lweapon") == 2 && one(d, "files[0]/llv") == 1 && one(d, "files[0]/lgold") == 2);
    CHECK(one(d, "files[0]/flags/106") == 1 && one(d, "files[0]/flags/50") == 4 && one(d, "files[0]/flags/2405") == 1);
    CHECK(q(d, "files[0]/flags/107").empty());   // zero flags are left out
    CHECK(q(d, "files[0]/othername[*]").size() == 6 && str(d, "files[0]/othername[0]").empty());
    // chapter 2+ layout (2500 flags, 5-member lists, 48 weapons, pocket items)
    CHECK(one(d, "files[name=filech2_0]/gold") == 1997 && one(d, "files[name=filech2_0]/time") == 50000.5);
    CHECK(q(d, "files[name=filech2_0]/weapon[*]").size() == 48 && one(d, "files[name=filech2_0]/weapon[0]") == 13);
    CHECK(q(d, "files[name=filech2_0]/pocketitem[*]").size() == 72 && q(d, "files[name=filech2_0]/hp[*]").size() == 5);
    CHECK(one(d, "files[name=filech2_0]/flags/325") == 1 && one(d, "files[name=filech2_0]/flags/2423") == 1);
    CHECK(q(d, "files[*]/error").empty());
    // ini files
    CHECK(str(d, "ini/dr.ini/G0/Name") == "Kris" && one(d, "ini/dr.ini/G0/Time") == 9000 && one(d, "ini/dr.ini/URA/1_0") == 1);
    CHECK(str(d, "ini/config_0.ini/LANG/LANG") == "en" && q(d, "ini/keyconfig_0").empty());
    // derived values
    CHECK(ones(d, "completed[*]") == "1");
    CHECK(ones(d, "patch[*]") == "5,23");
    CHECK(ones(d, "trophy[*]") == "1,5,9,14,18,22,23,27");
    CHECK(one(d, "trophy[0]") == 0);

    // every trophy from the patch, one file in an unknown layout -> CONNECTION
    std::vector<uint8_t> all = readFile(dir + "/deltarune_all.sav");
    DeltaruneDoc a;
    CHECK(a.parse(std::move(all)));
    printf("%s\n", a.info.summary.c_str());
    CHECK(str(a, "files[1]/error") == "unknown file layout");
    CHECK(one(a, "files[1]/plot") == 300 && one(a, "files[1]/room") == 42 && one(a, "files[1]/time") == 99.5);
    CHECK(one(a, "files[1]/flags/2429") == 1 && q(a, "files[1]/gold").empty());
    std::string all29;
    for (int i = 1; i < 30; i++) all29 += (i > 1 ? "," : "") + std::to_string(i);
    CHECK(ones(a, "patch[*]") == all29 && q(a, "patch[*]").size() == 30);
    CHECK(one(a, "trophy[0]") == 1 && ones(a, "completed[*]").empty());

    // damaged / foreign data fails cleanly
    for (size_t cut : {(size_t)1, (size_t)10, raw.size() / 2, raw.size() - 3}) {
        std::vector<uint8_t> t(raw.begin(), raw.begin() + cut);
        DeltaruneDoc x;
        CHECK(!x.parse(std::move(t)));
    }
    const char* other = "{\"playerData\":{\"geo\":5}}";
    CHECK(!isDeltarune((const uint8_t*)other, strlen(other)));
    std::string junk = "{\"filech1_0\":\"Kris\\r\\n2F010000FFFFFFFF\\r\\n2F01000001000000ZZ\\r\\n\",\"filech2_9\":7}";
    DeltaruneDoc j;
    CHECK(j.parse(std::vector<uint8_t>(junk.begin(), junk.end())));   // bad lists: no crash, file kept with an error
    CHECK(str(j, "files[0]/error") == "unknown file layout" && ones(j, "trophy[*]").empty());

    if (!dump.empty()) {
        for (const char* f : {"deltarune.sav", "deltarune_all.sav"}) {
            std::vector<uint8_t> b = readFile(dir + "/" + f);
            std::string err;
            cJSON* t = deltaruneToJson(b.data(), b.size(), err);
            char* s = t ? cJSON_PrintUnformatted(t) : nullptr;
            if (s) { std::ofstream(dump + "/" + f + ".json", std::ios::binary) << s; cJSON_free(s); }
            if (t) cJSON_Delete(t);
        }
    }
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
