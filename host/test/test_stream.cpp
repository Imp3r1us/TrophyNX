// The background module evaluates saves in "streaming" mode (small heap): chunked hash/value reads,
// offset reads for binary_le, tree files skipped when too big. This test checks that it gives exactly
// the same values as the app's whole-file mode.
//   python3 host/test/gen_saves.py /tmp/r romfs/games    (synthetic saves)
//   ./test_stream romfs/games /tmp/r/saves      (passwords of encrypted saves: /tmp/r/keys)
#include "rules.h"
#include <cstdio>
#include <cstring>

struct DirSource : SaveSource {
    std::string root, dir;
    bool stream;
    size_t avail;
    uint64_t reads = 0;
    DirSource(const std::string& r, bool s, size_t a) : root(r), stream(s), avail(a) {}
    bool open(uint64_t id) override {
        char b[32];
        snprintf(b, sizeof(b), "/%016llX/", (unsigned long long)id);
        dir = root + b;
        return !listDir(dir).empty();
    }
    void close() override {}
    std::string lastError() override { return "no save"; }
    bool readAll(const std::string& rel, std::vector<uint8_t>& out) override { return readFile(dir + rel, out); }
    bool streaming() const override { return stream; }
    bool fileSize(const std::string& rel, uint64_t& size) override {
        FILE* f = fopen((dir + rel).c_str(), "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        size = (uint64_t)ftell(f);
        fclose(f);
        return true;
    }
    bool readAt(const std::string& rel, uint64_t off, void* buf, size_t len) override {
        reads++;
        FILE* f = fopen((dir + rel).c_str(), "rb");
        if (!f) return false;
        bool ok = fseek(f, (long)off, SEEK_SET) == 0 && fread(buf, 1, len, f) == len;
        fclose(f);
        return ok;
    }
    size_t memoryAvailable() override { return avail; }
};

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: test_stream <definitions dir> <saves dir>\n"); return 0; }
    std::map<uint64_t, GameDef> defs;
    std::string errors;
    std::vector<std::string> loaded;
    loadGameDefs(argv[1], defs, errors, loaded);
    setSaveKeyDir(std::string(argv[2]) + "/../keys");
    int fails = 0, games = 0, compared = 0, met = 0;
    for (auto& kv : defs) {
        Game full, st, tiny, small;
        full.id = st.id = tiny.id = small.id = kv.first;
        applyGameDef(full, kv.second);
        applyGameDef(st, kv.second);
        applyGameDef(tiny, kv.second);
        applyGameDef(small, kv.second);
        // 384 KB: about what the live module has left while a game runs; readers that read in pieces must still work
        DirSource a(argv[2], false, (size_t)-1), b(argv[2], true, 64u << 20), c(argv[2], true, 1024), e(argv[2], true, 384u << 10);
        evaluateSave(full, a);
        if (full.saveStatus != SaveStatus::Ok) continue;
        evaluateSave(st, b);
        evaluateSave(tiny, c);
        evaluateSave(small, e);
        bool pieces = kv.second.reader == "byml" || kv.second.reader == "sgb";
        games++;
        bool tree = kv.second.reader == "json" || kv.second.reader == "byml" || kv.second.reader == "gvas" || kv.second.reader == "nrbf" || kv.second.reader == "sgb" ||
                    kv.second.reader == "deltarune" || kv.second.reader == "theatrhythm";
        for (size_t i = 0; i < full.achs.size(); i++) {
            if (!isSaveRule(full.achs[i].rule.type)) continue;
            compared++;
            if (full.achs[i].value + 1e-9 >= full.achs[i].rule.target) met++;
            if (full.achs[i].value != st.achs[i].value) {
                printf("FAIL %s %s: full %g, streamed %g\n", kv.second.name.c_str(), full.achs[i].id.c_str(), full.achs[i].value, st.achs[i].value);
                fails++;
            }
            if (pieces && full.achs[i].value != small.achs[i].value) {
                printf("FAIL %s %s with 384 KB: full %g, streamed %g\n", kv.second.name.c_str(), full.achs[i].id.c_str(), full.achs[i].value, small.achs[i].value);
                fails++;
            }
            // 1 KB "heap": tree files must be skipped (nothing unlocked), the others still work
            double expect = tree ? 0 : full.achs[i].value;
            if (tiny.achs[i].value != expect) {
                printf("FAIL %s %s with no memory: %g (expected %g)\n", kv.second.name.c_str(), full.achs[i].id.c_str(), tiny.achs[i].value, expect);
                fails++;
            }
        }
        printf("%-45s %-14s full = streamed (%llu reads)%s%s\n", kv.second.name.c_str(), kv.second.reader.c_str(), (unsigned long long)b.reads,
               pieces ? (", = 384 KB heap (" + std::to_string(e.reads) + " reads)").c_str() : "", tree ? ", skipped with no memory" : "");
    }
    printf("%d games, %d save achievements compared (%d met), %d failure(s)\n", games, compared, met, fails);
    return fails ? 1 : 0;
}
