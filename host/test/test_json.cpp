// g++ -std=gnu++17 -Iinclude host/test/test_json.cpp source/json_doc.cpp source/byml.cpp -x c source/cJSON.c -o /tmp/test_json && /tmp/test_json [user1.dat...]
// Checks the "json" reader's path queries on a built-in sample, then prints common Hollow Knight paths for any save given.
#include "json_doc.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

static int fails = 0;
static void check(bool c, const char* m) { printf("%s %s\n", c ? "ok  " : "FAIL", m); if (!c) fails++; }

static JsonDoc load(const std::string& s) {
    JsonDoc d;
    std::vector<uint8_t> b(s.begin(), s.end());
    d.parse(std::move(b));
    return d;
}
static std::vector<BymlValue> q(const JsonDoc& d, const char* p) { std::vector<BymlValue> v; d.query(p, v); return v; }

int main(int argc, char** argv) {
    const std::string sample = std::string("\xEF\xBB\xBF") +
        R"({"playerData":{"grubsCollected":46,"hasCharm":true,"completionPercentage":112.0,"name":"K",)"
        R"("bossDoorStateTier1":{"completed":true,"unlocked":true},"empty":null},)"
        R"("sceneData":{"persistentBoolItems":[{"id":"Battle Scene","sceneName":"Crossroads_04","activated":true},)"
        R"({"id":"Chest","sceneName":"X","activated":false}],"list":[1,2,3],"names":["a","b"]}})" + std::string("\0\0\n", 3);
    JsonDoc d = load(sample);
    check(d.error.empty() && d.topLevelCount() == 2, "parse with BOM and trailing NULs");
    auto v = q(d, "playerData/grubsCollected");
    check(v.size() == 1 && v[0].kind == BymlValue::Int && v[0].num == 46, "integer leaf");
    v = q(d, "playerData/hasCharm");
    check(v.size() == 1 && v[0].kind == BymlValue::Bool && v[0].num == 1, "bool leaf");
    v = q(d, "playerData/completionPercentage");
    check(v.size() == 1 && v[0].num == 112, "float leaf");
    v = q(d, "playerData/bossDoorStateTier1/completed");
    check(v.size() == 1 && v[0].num == 1, "nested object");
    v = q(d, "sceneData/persistentBoolItems[*]/activated");
    check(v.size() == 2 && v[0].truthy() && !v[1].truthy(), "[*] over objects");
    v = q(d, "sceneData/persistentBoolItems[id=Battle Scene]/activated");
    check(v.size() == 1 && v[0].num == 1, "[Field=value] selector");
    v = q(d, "sceneData/list[2]");
    check(v.size() == 1 && v[0].num == 3, "list index");
    v = q(d, "sceneData/list[7]");
    check(v.empty(), "index out of range");
    v = q(d, "sceneData/names[=b]");
    check(v.size() == 1 && v[0].kind == BymlValue::String, "[=value] on strings");
    v = q(d, "playerData/empty");
    check(v.size() == 1 && v[0].kind == BymlValue::Null && !v[0].truthy(), "null leaf");
    v = q(d, "playerData");
    check(v.size() == 1 && v[0].kind == BymlValue::Container && v[0].num == 6, "container leaf counts children");
    check(q(d, "playerData/missing").empty() && q(d, "playerData/grubsCollected/deeper").empty(), "missing paths match nothing");
    {   // values saved as text read as booleans / numbers (Slay the Spire "True", It Takes Two "12")
        JsonDoc t = load(R"({"a":"True","b":"12","c":"-3.5","d":"0012x","e":"FALSE","f":"","g":"1.","h":"12345678901234567"})");
        auto one = [&](const char* p) { auto r = q(t, p); return r.size() == 1 ? r[0] : BymlValue(); };
        check(one("a").kind == BymlValue::Bool && one("a").num == 1 && one("e").kind == BymlValue::Bool && one("e").num == 0, "\"True\"/\"FALSE\" -> bool");
        check(one("b").kind == BymlValue::Int && one("b").num == 12 && one("c").kind == BymlValue::Float && one("c").num == -3.5, "numeric text -> number");
        check(one("d").kind == BymlValue::String && one("f").kind == BymlValue::String && one("g").kind == BymlValue::String && one("h").kind == BymlValue::String, "other text stays text");
        std::vector<BymlValue> sel;
        JsonDoc l = load(R"({"Achievements":["ReachLV10","Survive1Minute"],"L":[{"Key":"Simple_A","Value":"True"}]})");
        l.query("Achievements[=ReachLV10]", sel);
        check(sel.size() == 1 && sel[0].truthy(), "string list membership");
        sel.clear(); l.query("L[Key=Simple_A]/Value", sel);
        check(sel.size() == 1 && sel[0].kind == BymlValue::Bool && sel[0].num == 1, "Key/Value list");
    }
    std::vector<BymlValue> none;
    check(!d.query("", none) && !d.query("a[1", none), "malformed path rejected");

    // moved-from documents stay valid; malformed input must not crash
    JsonDoc moved = std::move(d);
    check(q(moved, "playerData/grubsCollected").size() == 1 && q(d, "playerData/grubsCollected").empty(), "move");
    for (const char* bad : {"", "   ", "\0\0", "garbage", "{\"a\":", "[1,2", "{\"a\":[}", "nul"}) {
        JsonDoc b = load(bad);
        std::vector<BymlValue> o;
        b.query("a/b[*]", o);
        check(!b.error.empty(), (std::string("rejects: '") + bad + "'").c_str());
    }
    std::string deep(5000, '['); deep += std::string(5000, ']');
    { JsonDoc b = load(deep); std::vector<BymlValue> o; b.query("[0][0][0]", o); }
    printf("fuzz ok\n");

    const char* paths[] = {"playerData/version", "playerData/completionPercentage", "playerData/grubsCollected", "playerData/charmsOwned",
        "playerData/maxHealthBase", "playerData/MPReserveMax", "playerData/dreamOrbs", "playerData/killedHollowKnight",
        "playerData/bossDoorStateTier5/completed", "sceneData/persistentBoolItems[*]/activated"};
    for (int f = 1; f < argc; f++) {
        std::ifstream in(argv[f], std::ios::binary);
        std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), {});
        JsonDoc s;
        if (!s.parse(std::move(buf))) { printf("%s: %s\n", argv[f], s.error.c_str()); fails++; continue; }
        printf("== %s\n", argv[f]);
        for (const char* p : paths) {
            auto r = q(s, p);
            int truthy = 0; for (auto& x : r) truthy += x.truthy();
            printf("  %-45s matches %4zu  truthy %4d%s%s\n", p, r.size(), truthy, r.size() == 1 ? "  value " : "",
                   r.size() == 1 ? (r[0].kind == BymlValue::String ? r[0].str.c_str() : std::to_string(r[0].num).c_str()) : "");
        }
    }
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails;
}
