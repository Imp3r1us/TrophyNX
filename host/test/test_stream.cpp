// The background module evaluates saves in "streaming" mode (small heap): chunked hash/value reads,
// offset reads for binary_le, tree files skipped when too big. This test checks that it gives exactly
// the same values as the app's whole-file mode.
//   python3 host/test/gen_saves.py /tmp/r romfs/games    (synthetic saves)
//   ./test_stream romfs/games /tmp/r/saves /tmp/r/defs   (passwords of encrypted saves: /tmp/r/keys)
#include "rules.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

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
    size_t growTo = 0;      // > 0: reserveMemory may grow `avail` up to this (the module heap growing)
    int reserves = 0;
    bool reserveMemory(size_t bytes) override {
        if (!growTo || bytes > growTo) return false;
        reserves++;
        if (bytes > avail) avail = bytes;
        return true;
    }
};

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: test_stream <definitions dir> <saves dir>\n"); return 0; }
    std::map<uint64_t, GameDef> defs;
    std::string errors;
    std::vector<std::string> loaded;
    loadGameDefs(argv[1], defs, errors, loaded);
    if (argc > 3) loadGameDefs(argv[3], defs, errors, loaded);   // gen_saves.py's fixture games (every tree reader)
    setSaveKeyDir(std::string(argv[2]) + "/../keys");
    int fails = 0, games = 0, compared = 0, met = 0;
    for (auto& kv : defs) {
        Game full, st, tiny, small, grow;
        full.id = st.id = tiny.id = small.id = grow.id = kv.first;
        applyGameDef(full, kv.second);
        applyGameDef(st, kv.second);
        applyGameDef(tiny, kv.second);
        applyGameDef(small, kv.second);
        applyGameDef(grow, kv.second);
        // 384 KB: about what the live module has left while a game runs; readers that read in pieces must still work
        DirSource a(argv[2], false, (size_t)-1), b(argv[2], true, 64u << 20), c(argv[2], true, 1024), e(argv[2], true, 384u << 10), h(argv[2], true, 48u << 10);
        h.growTo = 16u << 20;   // almost nothing free, but the heap can grow to 16 MB (like the module)
        evaluateSave(full, a);
        if (full.saveStatus != SaveStatus::Ok) continue;
        evaluateSave(st, b);
        evaluateSave(tiny, c);
        evaluateSave(small, e);
        evaluateSave(grow, h);
        bool pieces = kv.second.reader == "byml" || kv.second.reader == "sgb" || kv.second.reader == "json";
        games++;
        bool tree = kv.second.reader != "binary_le" && kv.second.reader != "gamedata_hash" && kv.second.reader != "totk_progress";
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
            if (full.achs[i].value != grow.achs[i].value) {   // every reader, whatever the save size
                printf("FAIL %s %s with 48 KB + growing: full %g, streamed %g\n", kv.second.name.c_str(), full.achs[i].id.c_str(), full.achs[i].value, grow.achs[i].value);
                fails++;
            }
            // 1 KB "heap": tree files must be skipped (nothing unlocked), the others still work
            double expect = tree ? 0 : full.achs[i].value;
            if (tiny.achs[i].value != expect) {
                printf("FAIL %s %s with no memory: %g (expected %g)\n", kv.second.name.c_str(), full.achs[i].id.c_str(), tiny.achs[i].value, expect);
                fails++;
            }
        }
        if (getenv("STREAM_DEBUG")) for (const std::string& l : grow.debug) printf("    | %s\n", l.c_str());
        printf("%-45s %-14s full = streamed (%llu reads)%s%s\n", kv.second.name.c_str(), kv.second.reader.c_str(), (unsigned long long)b.reads,
               pieces ? (", = 384 KB heap (" + std::to_string(e.reads) + " reads)").c_str() :
               h.reserves ? (", = 48 KB heap grown " + std::to_string(h.reserves) + "x").c_str() : "", tree ? ", skipped with no memory" : "");
    }
    printf("%d games, %d save achievements compared (%d met), %d failure(s)\n", games, compared, met, fails);
    return fails ? 1 : 0;
}
