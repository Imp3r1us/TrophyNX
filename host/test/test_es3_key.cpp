// Save password search read in pieces (es3FindPasswordIn, theatrhythmFindPasswordIn) and the live module's
// automatic search when a game with an encrypted save closes (findKeyFromGame through a GameFileOpener).
// Fixture: tools/savelab/test/fixtures/es3 (synthetic save, DUMMY password).
// g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_es3_key.cpp source/rules.cpp source/memrules.cpp source/fileutil.cpp source/save_formats.cpp source/json_doc.cpp source/byml.cpp -x c source/cJSON.c -o /tmp/test_es3_key
#include "rules.h"
#include "save_formats.h"
#include "platform.h"
#include "cJSON.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <unistd.h>

static int fails = 0;
static void check(bool c, const char* m) { printf("%s %s\n", c ? "ok  " : "FAIL", m); if (!c) fails++; }
static std::vector<uint8_t> slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static const std::string FX = "tools/savelab/test/fixtures/es3/";

// resources.assets look-alike: junk, then "ES3Defaults" at `at`, a few fields, then the password (u32 length + text).
static std::vector<uint8_t> makeAsset(size_t total, size_t at, const std::string& pw) {
    std::vector<uint8_t> a(total);
    for (size_t i = 0; i < total; i++) a[i] = (uint8_t)(i * 131 + 7);
    auto put = [&](size_t& o, const std::string& s) {
        uint32_t n = (uint32_t)s.size();
        memcpy(&a[o], &n, 4);
        memcpy(&a[o + 4], s.data(), s.size());
        o += 4 + ((s.size() + 3) & ~(size_t)3);
    };
    memcpy(&a[at], "ES3Defaults", 11);
    size_t o = at + 12;
    put(o, "SaveFile.es3");
    put(o, "not the password");
    put(o, pw);
    return a;
}

struct CountingReader {
    const std::vector<uint8_t>* d;
    size_t biggest = 0, calls = 0;
    bool fail = false;
    ByteReader fn() {
        return [this](uint64_t off, void* buf, size_t n) {
            calls++;
            if (n > biggest) biggest = n;
            if (fail || off > d->size() || n > d->size() - off) return false;
            memcpy(buf, d->data() + off, n);
            return true;
        };
    }
};

// Save source over the fixture folder; streaming() like the live module when asked.
class DirSource : public SaveSource {
public:
    std::string dir;
    bool stream = false;
    bool open(uint64_t) override { return true; }
    void close() override {}
    std::string lastError() override { return ""; }
    bool streaming() const override { return stream; }
    bool fileSize(const std::string& rel, uint64_t& size) override {
        std::vector<uint8_t> b = slurp(dir + rel);
        size = b.size();
        return !b.empty();
    }
    bool readAt(const std::string& rel, uint64_t off, void* buf, size_t len) override {
        std::vector<uint8_t> b = slurp(dir + rel);
        if (off > b.size() || len > b.size() - off) return false;
        memcpy(buf, b.data() + off, len);
        return true;
    }
    bool readAll(const std::string& rel, std::vector<uint8_t>& out) override {
        out = slurp(dir + rel);
        return !out.empty();
    }
};

int main() {
    std::vector<uint8_t> pwb = slurp(FX + "password.txt");
    std::string pw(pwb.begin(), pwb.end());
    while (!pw.empty() && (pw.back() == '\n' || pw.back() == '\r')) pw.pop_back();
    std::vector<uint8_t> save = slurp(FX + "SaveFile.es3");
    check(!pw.empty() && save.size() == 96, "fixture: dummy password and save");
    {
        std::string err;
        cJSON* t = es3ToJson(save.data(), save.size(), pw, err);
        check(t && cJSON_GetObjectItem(cJSON_GetObjectItem(t, "stars"), "value")->valueint == 42, "fixture decrypts with its password");
        cJSON_Delete(t);
    }

    // ---- in pieces: same result as the whole file, wherever the tag falls
    const size_t chunk = 256 * 1024;
    const size_t spots[] = {100, chunk - 5, chunk - 11, chunk + 3, 3 * chunk - 600, 3 * chunk + 1000 - 80};
    for (size_t at : spots) {
        std::vector<uint8_t> asset = makeAsset(3 * chunk + 1000, at, pw);
        std::string whole, pieces;
        CountingReader cr{&asset};
        bool a = es3FindPassword(asset.data(), asset.size(), save.data(), save.size(), whole);
        bool b = es3FindPasswordIn(cr.fn(), asset.size(), save.data(), save.size(), pieces);
        char m[96];
        snprintf(m, sizeof m, "ES3Defaults at %zu: found whole and in pieces", at);
        check(a && b && whole == pw && pieces == pw, m);
        check(cr.biggest <= chunk + 2048, "never more than one piece in memory");
    }
    {
        std::vector<uint8_t> asset = makeAsset(chunk, 1000, "wrong password");
        std::string found;
        CountingReader cr{&asset};
        check(!es3FindPasswordIn(cr.fn(), asset.size(), save.data(), save.size(), found), "other password: nothing found");
        std::vector<uint8_t> good = makeAsset(2 * chunk, chunk + 50, pw);
        CountingReader bad{&good};
        bad.fail = true;
        check(!es3FindPasswordIn(bad.fn(), good.size(), save.data(), save.size(), found), "read error: nothing found, no crash");
    }

    // ---- the live module: game file opened through the opener, key file written once
    const std::string def =
        "{\"title_id\":\"01004CA01B77E000\",\"name\":\"ES3 test\",\"reader\":\"es3\",\"key_file\":\"es3test.txt\","
        "\"save_files\":[\"SaveFile.es3\"],\"achievements\":[{\"id\":\"stars\",\"name\":\"Stars\",\"desc\":\"\",\"points\":5,"
        "\"rule\":{\"type\":\"flag\",\"path\":\"stars/value\",\"target\":40}}]}";
    GameDef gd;
    std::string err;
    check(parseGameDef(def.c_str(), gd, err) && gd.keyFile == "es3test.txt", "definition with key_file");
    const std::string tmp = "es3_test_keys";
    makeDirs(tmp);
    remove((tmp + "/es3test.txt").c_str());
    setSaveKeyDir(tmp);
    setSaveKeySearchDirs({});
    std::vector<uint8_t> asset = makeAsset(2 * chunk + 77, chunk - 3, pw);
    int opens = 0;
    std::string openedRel;
    setSaveKeyGameReader([&](uint64_t tid, const std::string& rel, GameFile& out, std::string& why) {
        opens++;
        openedRel = rel;
        if (tid != 0x01004CA01B77E000ULL) { why = "wrong title"; return false; }
        out.size = asset.size();
        out.read = [&](uint64_t off, void* buf, size_t n) {
            if (off > asset.size() || n > asset.size() - off) return false;
            memcpy(buf, asset.data() + off, n);
            return true;
        };
        out.close = [] {};
        return true;
    });
    DirSource src;
    src.dir = FX;
    src.stream = true;
    {
        Game g;
        g.id = gd.id;
        applyGameDef(g, gd);
        evaluateSave(g, src);
        std::vector<uint8_t> written = slurp(tmp + "/es3test.txt");
        check(opens == 1 && openedRel == "Data/resources.assets", "module: resources.assets opened from the installed game");
        check(std::string(written.begin(), written.end()) == pw, "module: password found and key file written");
        check(g.saveStatus == SaveStatus::Ok && !g.achs.empty() && g.achs[0].value == 42, "module: save read with it at once");
    }
    remove((tmp + "/es3test.txt").c_str());
    {
        Game g;
        g.id = gd.id;
        applyGameDef(g, gd);
        evaluateSave(g, src);
        check(opens == 1 && g.saveStatus == SaveStatus::NeedsKey, "module: searched once per boot (no new search on the next save)");
    }
    {
        // the app (not streaming) searches again each time
        src.stream = false;
        Game g;
        g.id = gd.id;
        applyGameDef(g, gd);
        evaluateSave(g, src);
        check(opens == 2 && g.saveStatus == SaveStatus::Ok, "app: searches again");
    }
    remove((tmp + "/es3test.txt").c_str());
    rmdir(tmp.c_str());
    setSaveKeyDir("");
    setSaveKeyGameReader(nullptr);

    // ---- THEATRHYTHM metadata in pieces: same answer as the whole file
    {
        std::vector<uint8_t> meta(64, 0);
        std::string found;
        CountingReader cr{&meta};
        check(!theatrhythmFindPasswordIn(cr.fn(), meta.size(), save.data(), save.size(), found), "theatrhythm: not a metadata file");
    }
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
