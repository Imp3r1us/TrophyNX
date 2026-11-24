// Live save watch: reading a save image out of a raw FAT32 partition (source/rawfat.cpp) and the files out of
// the image (source/saveimage.cpp). Fixtures from host/test/make_save_image.py.
// usage: test_saveimage <dir made by make_save_image.py>...   (no argument: junk input only)
#include "rawfat.h"
#include "rules.h"
#include "saveimage.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); g_fail++; } } while (0)

static bool slurp(const std::string& p, std::vector<uint8_t>& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    bool ok = fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

static saveimg::ReadFn memReader(const std::vector<uint8_t>& v, int* calls = nullptr) {
    return [&v, calls](uint64_t off, void* buf, size_t len) {
        if (calls) (*calls)++;
        if (off > v.size() || len > v.size() - off) return false;
        memcpy(buf, v.data() + off, len);
        return true;
    };
}

static void checkFiles(saveimg::Image& img, const std::string& dir, const char* what) {
    const char* names[] = {"progress.json", "slot0/game.sav", "slot0/sub/deep.bin", "empty.dat", "sparse.bin"};
    CHECK(img.files().size() == 5, "%s: %u files", what, (unsigned)img.files().size());
    for (const char* n : names) {
        std::vector<uint8_t> want, got;
        CHECK(slurp(dir + "/expected/" + n, want), "%s: no expected %s", what, n);
        const saveimg::FileInfo* f = img.find(n);
        CHECK(f, "%s: %s not found (%s)", what, n, img.error().c_str());
        if (!f) continue;
        CHECK(f->size == want.size(), "%s: %s size %llu, want %u", what, n, (unsigned long long)f->size, (unsigned)want.size());
        got.resize((size_t)f->size);
        CHECK(img.readFile(*f, 0, got.data(), got.size()), "%s: read %s: %s", what, n, img.error().c_str());
        CHECK(got == want, "%s: %s content differs", what, n);
        // small reads at odd offsets (what binary rules do)
        for (uint64_t off = 0; off + 7 <= want.size(); off += 4093) {
            uint8_t b[7];
            CHECK(img.readFile(*f, off, b, 7) && memcmp(b, want.data() + off, 7) == 0, "%s: %s at %llu", what, n, (unsigned long long)off);
        }
    }
    uint8_t b;
    const saveimg::FileInfo* f = img.find("progress.json");
    CHECK(f && !img.readFile(*f, f->size, &b, 1), "%s: read past the end accepted", what);
    CHECK(!img.find("nope.bin") && !img.find("slot0"), "%s: found a file that isn't there", what);
    CHECK(img.find("/slot0/game.sav"), "%s: leading slash", what);
}

static void fixture(const std::string& dir) {
    std::vector<uint8_t> nand, image;
    CHECK(slurp(dir + "/nand.img", nand) && slurp(dir + "/save.img", image), "%s: missing fixture", dir.c_str());
    char name[64] = {};
    FILE* f = fopen((dir + "/info.txt").c_str(), "r");
    CHECK(f && fscanf(f, "%63s", name) == 1, "info.txt");
    if (f) fclose(f);

    // 1) the image straight from its file
    saveimg::Image img;
    CHECK(img.open(memReader(image)), "%s: open image: %s", dir.c_str(), img.error().c_str());
    checkFiles(img, dir, "image");
    std::string commit = saveimg::commitId(memReader(image));
    CHECK(!commit.empty() && commit == img.commit(), "commit id");

    // 2) the same image found in the FAT32 volume (sector-aligned reads only)
    bool aligned = true;
    rawfat::ReadFn rd = [&](uint64_t off, void* buf, size_t len) {
        if (off % 512 || len % 512) aligned = false;
        if (off > nand.size() || len > nand.size() - off) return false;
        memcpy(buf, nand.data() + off, len);
        return true;
    };
    rawfat::Volume vol;
    CHECK(vol.mount(rd), "mount: %s", vol.error().c_str());
    rawfat::File file;
    CHECK(vol.open(std::string("/save/") + name, file), "open /save/%s: %s", name, vol.error().c_str());
    CHECK(file.size == image.size(), "FAT32 file size %llu vs %u", (unsigned long long)file.size, (unsigned)image.size());
    CHECK(file.runs.size() > 1, "the fixture's image should be fragmented");
    std::string upper = name;
    for (char& c : upper) c = (char)toupper(c);
    rawfat::File again;
    CHECK(vol.open("save/" + upper, again) && again.runs == file.runs, "case-insensitive lookup");
    rawfat::File none;
    CHECK(!vol.open("/save/0000000000000000", none), "missing save found");
    CHECK(!vol.open("/save", none), "a directory opened as a file");
    std::vector<uint8_t> whole(image.size());
    CHECK(file.read(rd, 0, whole.data(), whole.size()) && whole == image, "image bytes through FAT32 differ");
    saveimg::ReadFn imgRead = [&](uint64_t off, void* buf, size_t len) { return file.read(rd, off, buf, len); };
    saveimg::Image img2;
    CHECK(img2.open(imgRead), "open image through FAT32: %s", img2.error().c_str());
    checkFiles(img2, dir, "fat32");
    CHECK(aligned, "unaligned partition read");

    // 3) SaveSource
    saveimg::ImageSaveSource src(imgRead, [] { return (size_t)100000; });
    CHECK(src.open(0), "source open: %s", src.lastError().c_str());
    uint64_t size = 0;
    std::vector<uint8_t> buf;
    CHECK(src.fileSize("progress.json", size) && size > 0, "source size");
    CHECK(src.readAll("progress.json", buf) && buf.size() == size, "source readAll");
    CHECK(!src.readAll("slot0/game.sav", buf), "readAll ignored the memory limit");
    char c4[4];
    CHECK(src.readAt("slot0/sub/deep.bin", 4, c4, 4) && memcmp(c4, "deep", 4) == 0, "source readAt");
    CHECK(src.commit() == commit, "source commit");
    src.close();
    CHECK(src.commit() == commit, "source commit still known after close (live save reads it then)");

    // 4) the rule engine on top of it, as the module does (streaming, small memory)
    const char* def = R"({"title_id": "0100000000000001", "name": "Test", "reader": "json", "save_files": ["progress.json"],
        "achievements": [
          {"id": "rich", "name": "Rich", "desc": "1000 gold", "points": 10, "rule": {"type": "flag", "path": "gold", "target": 1000}},
          {"id": "ch5", "name": "Chapter 5", "desc": "", "points": 10, "rule": {"type": "flag", "path": "chapter", "target": 5}}]})";
    GameDef gd;
    std::string err;
    CHECK(parseGameDef(def, gd, err), "definition: %s", err.c_str());
    CHECK(gd.liveSave, "live_save defaults to on");
    Game g;
    g.id = gd.id;
    applyGameDef(g, gd);
    saveimg::ImageSaveSource rsrc(imgRead, [] { return (size_t)1 << 20; });
    evaluateSave(g, rsrc);
    CHECK(g.saveStatus == SaveStatus::Ok, "rules: save status %d", (int)g.saveStatus);
    // live save (sysmodule/source/livesave.cpp readSave) compares this with the header after evaluateSave,
    // which closes the source: an empty id here made every live save read look like "the game saved meanwhile"
    CHECK(rsrc.commit() == commit, "commit after evaluateSave: '%s'", rsrc.commit().c_str());
    for (const Achievement& a : g.achs) {
        bool met = a.value + 1e-9 >= a.rule.target;
        CHECK(met == (a.id == "rich"), "rules: %s value %g", a.id.c_str(), a.value);
    }

    // 5) a header changed under the reader (the game saving) -> different commit, broken -> refused
    std::vector<uint8_t> bad = image;
    for (size_t i = 0x300; i < 0x8000; i += 97) bad[i] ^= 0x5A;
    saveimg::Image img3;
    CHECK(!img3.open(memReader(bad)), "corrupt headers accepted");
    CHECK(saveimg::commitId(memReader(bad)).empty(), "commit id of corrupt headers");
    printf("ok   %s (%u runs)\n", dir.c_str(), (unsigned)file.runs.size());
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) fixture(argv[i]);
    // junk never crashes or hangs
    std::mt19937 rng(42);
    for (int round = 0; round < 200; round++) {
        std::vector<uint8_t> junk(0x10000);
        for (auto& b : junk) b = (uint8_t)rng();
        if (round % 2) { memcpy(&junk[0x100], "DISF", 4); memcpy(&junk[0x1FE], "\x55\xAA", 2); memcpy(&junk[0x52], "FAT32", 5); }
        saveimg::Image img;
        CHECK(!img.open(memReader(junk)), "junk accepted as a save image");
        rawfat::Volume v;
        rawfat::File f;
        if (v.mount(memReader(junk))) v.open("/save/0000000000000001", f);
    }
    printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
