// Switch save data container reader, see include/saveimage.h.
//
// Storage stack (hactool save.c), from a file of the save down to the image:
//   file (FAT chain of blocks) -> core data = IVFC data level -> journal (virtual -> physical blocks)
//   -> data remap -> image.
// The tables (FAT, journal map, IVFC hashes) live in the meta remap -> duplex data layer
//   (A/B copy per block, chosen by the L1 bitmap, itself chosen by the master bitmap in the header)
//   -> data remap -> image.
#include "saveimage.h"
#include "sha256.h"
#include <algorithm>
#include <cstring>

namespace saveimg {

static const size_t HDR = 0x4000;
static const uint32_t CHUNK = 0x4000;   // image cache granularity
static const uint32_t MAGIC_DISF = 0x46534944, MAGIC_DPFS = 0x53465044, MAGIC_IVFC = 0x43465649,
                      MAGIC_JNGL = 0x4C474E4A, MAGIC_SAVE = 0x45564153, MAGIC_RMAP = 0x50414D52;

static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le64(const uint8_t* p) { return le32(p) | ((uint64_t)le32(p + 4) << 32); }

// header offsets
static const size_t L_HASH = 0x108;                       // layout hash = SHA-256 of header[0x300..0x4000]
static uint64_t layoutField(const std::vector<uint8_t>& h, int k) { return le64(&h[0x128 + 8 * k]); }
enum {
    F_FILE_MAP_ENTRY_OFF = 0, F_META_MAP_ENTRY_OFF = 2, F_FILE_MAP_DATA_OFF = 4,
    F_L1_OFF_A = 6, F_L1_OFF_B = 7, F_L1_SIZE = 8, F_DATA_OFF_A = 9, F_DATA_OFF_B = 10, F_DATA_SIZE = 11,
    F_JOURNAL_DATA_OFF = 12, F_MASTER_OFF_A = 16, F_MASTER_OFF_B = 17, F_MASTER_SIZE = 18,
    F_JMAP_OFF = 22, F_JMAP_SIZE = 23, F_FAT_OFF = 36, F_FAT_SIZE = 37, F_DUPLEX_INDEX = 38,
};
static const size_t DPFS = 0x300, DATA_IVFC = 0x344, JNGL = 0x408, JMAP = 0x428, SAVE = 0x608, FATH = 0x620,
                    MAIN_RMAP = 0x650, META_RMAP = 0x690, FAT_IVFC = 0xAD8;

static std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

// A header copy is usable when its magics are right and its hash matches.
static bool headerValid(const std::vector<uint8_t>& h) {
    if (h.size() < HDR) return false;
    if (le32(&h[0x100]) != MAGIC_DISF || le32(&h[DPFS]) != MAGIC_DPFS || le32(&h[DATA_IVFC]) != MAGIC_IVFC ||
        le32(&h[JNGL]) != MAGIC_JNGL || le32(&h[SAVE]) != MAGIC_SAVE || le32(&h[MAIN_RMAP]) != MAGIC_RMAP ||
        le32(&h[META_RMAP]) != MAGIC_RMAP)
        return false;
    return sha256Hex(&h[0x300], HDR - 0x300) == hex(&h[L_HASH], 0x20);
}

// Header A if valid, else header B (like hactool).
static bool pickHeader(const ReadFn& read, std::vector<uint8_t>& out) {
    out.assign(HDR, 0);
    if (read(0, out.data(), HDR) && headerValid(out)) return true;
    if (read(HDR, out.data(), HDR) && headerValid(out)) return true;
    out.clear();
    return false;
}

std::string commitId(const ReadFn& read) {
    std::vector<uint8_t> h;
    return pickHeader(read, h) ? hex(&h[L_HASH], 0x20) : std::string();
}

// Bit i of a duplex bitmap: little-endian u32 words, most significant bit first.
static bool bitAt(const std::vector<uint8_t>& bm, uint64_t i) {
    uint64_t w = (i / 32) * 4;
    if (w + 4 > bm.size()) return false;
    return (le32(&bm[w]) >> (31 - (i % 32))) & 1;
}

// ---------------------------------------------------------------- storages
bool Image::readBase(uint64_t off, void* buf, size_t len) {
    uint8_t* out = (uint8_t*)buf;
    while (len) {
        uint64_t c = off / CHUNK * CHUNK;
        Chunk* hit = nullptr;
        for (Chunk& ch : m_cache) if (ch.off == c && !ch.data.empty()) { hit = &ch; break; }
        if (!hit) {
            hit = &m_cache[0];
            for (Chunk& ch : m_cache) if (ch.data.empty() || ch.age < hit->age) hit = &ch;
            hit->data.resize(CHUNK);
            if (!m_read(c, hit->data.data(), CHUNK)) { hit->off = ~0ULL; return false; }
            hit->off = c;
        }
        hit->age = ++m_clock;
        size_t pos = (size_t)(off - c), n = std::min<size_t>(len, CHUNK - pos);
        memcpy(out, hit->data.data() + pos, n);
        out += n; off += n; len -= n;
    }
    return true;
}

bool Image::readDataRemap(uint64_t off, void* buf, size_t len) {
    uint8_t* out = (uint8_t*)buf;
    while (len) {
        const RemapEntry* e = nullptr;
        for (const RemapEntry& r : m_dataRemap.entries) if (off >= r.virt && off < r.virt + r.size) { e = &r; break; }
        if (!e) return fail("data remap: offset out of range");
        size_t n = (size_t)std::min<uint64_t>(len, e->virt + e->size - off);
        if (!readBase(m_dataRemap.base + e->phys + (off - e->virt), out, n)) return fail("image read failed");
        out += n; off += n; len -= n;
    }
    return true;
}

bool Image::readDuplex(uint64_t off, void* buf, size_t len) {
    uint8_t* out = (uint8_t*)buf;
    const uint64_t bs = 1ULL << m_dataPower;
    while (len) {
        uint64_t block = off / bs;
        size_t n = (size_t)std::min<uint64_t>(len, bs - off % bs);
        uint64_t layer = bitAt(m_l1, block) ? m_duplexB : m_duplexA;
        if (!readDataRemap(layer + off, out, n)) return false;
        out += n; off += n; len -= n;
    }
    return true;
}

bool Image::readMetaRemap(uint64_t off, void* buf, size_t len) {
    uint8_t* out = (uint8_t*)buf;
    while (len) {
        const RemapEntry* e = nullptr;
        for (const RemapEntry& r : m_metaRemap.entries) if (off >= r.virt && off < r.virt + r.size) { e = &r; break; }
        if (!e) return fail("meta remap: offset out of range");
        size_t n = (size_t)std::min<uint64_t>(len, e->virt + e->size - off);
        if (!readDuplex(e->phys + (off - e->virt), out, n)) return false;
        out += n; off += n; len -= n;
    }
    return true;
}

bool Image::readJournal(uint64_t off, void* buf, size_t len) {
    uint8_t* out = (uint8_t*)buf;
    while (len) {
        uint64_t block = off / m_journalBlock;
        if (block >= m_journalMap.size()) return fail("journal: block out of range");
        size_t n = (size_t)std::min<uint64_t>(len, m_journalBlock - off % m_journalBlock);
        uint64_t phys = (uint64_t)m_journalMap[block] * m_journalBlock + off % m_journalBlock;
        if (!readDataRemap(m_journalDataOff + phys, out, n)) return false;
        out += n; off += n; len -= n;
    }
    return true;
}

// IVFC data level. A block whose hash is all zeros was never written: it reads as zeros (as in FS).
bool Image::readCoreData(uint64_t off, void* buf, size_t len) {
    uint8_t* out = (uint8_t*)buf;
    const uint64_t bs = 1ULL << m_ivfcPower;
    while (len) {
        if (off >= m_ivfcDataSize) return fail("core data: offset out of range");
        uint64_t block = off / bs;
        size_t n = (size_t)std::min<uint64_t>(len, bs - off % bs);
        uint8_t hash[0x20];
        if (!readMetaRemap(m_ivfcHashOff + block * 0x20, hash, sizeof(hash))) return false;
        bool zero = true;
        for (uint8_t b : hash) if (b) { zero = false; break; }
        if (zero) memset(out, 0, n);
        else if (!readJournal(m_ivfcDataOff + off, out, n)) return false;
        out += n; off += n; len -= n;
    }
    return true;
}

// ---------------------------------------------------------------- allocation table
// Entry i+1 describes block i: {prev, next}. next with the top bit set = first block of a range whose
// last entry index is in the next entry's "next". prev 0x80000000 = start of a list, next 0 = end.
bool Image::chainSegments(uint32_t start, std::vector<std::pair<uint32_t, uint32_t>>& segs) {
    if (start == m_segStart) { segs = m_segs; return true; }
    segs.clear();
    const size_t entries = m_fat.size() / 8;
    uint32_t block = start;
    for (uint32_t guard = 0; block != 0xFFFFFFFFu; guard++) {
        if (guard > m_fatBlocks + 1) return fail("allocation table: cycle");
        uint64_t ei = (uint64_t)block + 1;
        if (ei >= entries) return fail("allocation table: block out of range");
        const uint8_t* e0 = &m_fat[ei * 8];
        uint32_t prev = le32(e0), next = le32(e0 + 4);
        uint32_t length = 1;
        if (next & 0x80000000u) {
            if (ei + 1 >= entries) return fail("allocation table: bad range");
            uint32_t last = le32(&m_fat[(ei + 1) * 8 + 4]) & 0x7FFFFFFFu;
            if (last < ei) return fail("allocation table: bad range");
            length = (uint32_t)(last - ei + 1);
        }
        if (segs.empty() && prev != 0x80000000u) return fail("allocation table: not the start of a list");
        segs.push_back({block, length});
        block = (next & 0x7FFFFFFFu) == 0 ? 0xFFFFFFFFu : (next & 0x7FFFFFFFu) - 1;
    }
    m_segStart = start;
    m_segs = segs;
    return true;
}

bool Image::readChain(uint32_t start, uint64_t off, void* buf, size_t len) {
    std::vector<std::pair<uint32_t, uint32_t>> segs;
    if (!chainSegments(start, segs)) return false;
    uint8_t* out = (uint8_t*)buf;
    uint64_t virtStart = 0;   // first byte of the current segment in the chain
    for (const auto& s : segs) {
        uint64_t segLen = (uint64_t)s.second * m_blockSize;
        while (len && off < virtStart + segLen) {
            uint64_t pos = off - virtStart;
            size_t n = (size_t)std::min<uint64_t>(len, segLen - pos);
            if (!readCoreData((uint64_t)s.first * m_blockSize + pos, out, n)) return false;
            out += n; off += n; len -= n;
        }
        if (!len) return true;
        virtStart += segLen;
    }
    return fail("read past the end of a chain");
}

// ---------------------------------------------------------------- open
bool Image::loadRemap(Remap& r, uint64_t entriesOff, uint32_t count, uint64_t base) {
    if (count > 0x10000) return fail("remap: too many entries");
    r.entries.clear();
    r.base = base;
    std::vector<uint8_t> raw((size_t)count * 0x20);
    if (count && !readBase(entriesOff, raw.data(), raw.size())) return fail("image read failed (remap table)");
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* p = &raw[i * 0x20];
        r.entries.push_back({le64(p), le64(p + 8), le64(p + 16)});
    }
    return true;
}

bool Image::open(ReadFn read) {
    close();
    m_read = std::move(read);
    if (!pickHeader(m_read, m_hdr)) return fail("no valid save header (not a save image, or read while the game was saving)");
    const std::vector<uint8_t>& h = m_hdr;
    m_commit = hex(&h[L_HASH], 0x20);

    // data remap: image -> virtual space holding the duplex layers and the journal
    if (!loadRemap(m_dataRemap, layoutField(h, F_FILE_MAP_ENTRY_OFF), le32(&h[MAIN_RMAP + 8]),
                   layoutField(h, F_FILE_MAP_DATA_OFF)))
        return false;

    // hierarchical duplex: master bitmap (in the header) -> L1 bitmap -> data layer
    uint64_t masterOff = layoutField(h, layoutField(h, F_DUPLEX_INDEX) == 1 ? F_MASTER_OFF_B : F_MASTER_OFF_A);
    uint64_t masterSize = layoutField(h, F_MASTER_SIZE);
    if (masterOff + masterSize > HDR) return fail("duplex: master bitmap outside the header");
    std::vector<uint8_t> master(h.begin() + masterOff, h.begin() + masterOff + masterSize);
    uint32_t p1 = le32(&h[DPFS + 8 + 0x14 * 1 + 16]), p2 = le32(&h[DPFS + 8 + 0x14 * 2 + 16]);
    if (p1 < 6 || p1 > 24 || p2 < 6 || p2 > 24) return fail("duplex: bad block size");
    m_l1Power = p1;
    m_dataPower = p2;
    uint64_t l1Size = layoutField(h, F_L1_SIZE), l1A = layoutField(h, F_L1_OFF_A), l1B = layoutField(h, F_L1_OFF_B);
    if (l1Size > 0x100000) return fail("duplex: L1 bitmap too big");
    m_l1.assign((size_t)l1Size, 0);
    const uint64_t b1 = 1ULL << m_l1Power;
    for (uint64_t off = 0; off < l1Size;) {
        uint64_t block = off / b1;
        size_t n = (size_t)std::min<uint64_t>(l1Size - off, b1 - off % b1);
        if (!readDataRemap((bitAt(master, block) ? l1B : l1A) + off, &m_l1[off], n)) return false;
        off += n;
    }
    m_duplexA = layoutField(h, F_DATA_OFF_A);
    m_duplexB = layoutField(h, F_DATA_OFF_B);

    // meta remap: duplex data layer -> virtual space holding the journal map, the IVFC hashes and the FAT
    if (!loadRemap(m_metaRemap, layoutField(h, F_META_MAP_ENTRY_OFF), le32(&h[META_RMAP + 8]), 0)) return false;

    // journal: virtual block -> physical block
    m_journalBlock = le64(&h[JNGL + 0x18]);
    uint32_t mainBlocks = le32(&h[JMAP + 4]);
    if (m_journalBlock < 0x200 || m_journalBlock > 0x100000 || mainBlocks > 0x400000) return fail("journal: bad header");
    if ((uint64_t)mainBlocks * 8 > layoutField(h, F_JMAP_SIZE)) return fail("journal: map larger than its table");
    m_journalDataOff = layoutField(h, F_JOURNAL_DATA_OFF);
    m_journalMap.assign(mainBlocks, 0);
    {
        uint64_t base = layoutField(h, F_JMAP_OFF);
        std::vector<uint8_t> raw(0x1000);
        for (uint32_t i = 0; i < mainBlocks;) {
            uint32_t n = std::min<uint32_t>(mainBlocks - i, 0x200);
            if (!readMetaRemap(base + (uint64_t)i * 8, raw.data(), n * 8)) return false;
            for (uint32_t k = 0; k < n; k++) m_journalMap[i + k] = le32(&raw[k * 8]) & 0x7FFFFFFFu;
            i += n;
        }
    }

    // IVFC: data level (in the journal) and the level of its hashes (in the meta remap)
    const uint8_t* lv = &h[DATA_IVFC + 0x10];   // level headers: {offset, size, block size power, reserved}
    m_ivfcHashOff = le64(lv + 0x18 * 2);
    m_ivfcDataOff = le64(lv + 0x18 * 3);
    m_ivfcDataSize = le64(lv + 0x18 * 3 + 8);
    m_ivfcPower = le32(lv + 0x18 * 3 + 16);
    if (m_ivfcPower < 6 || m_ivfcPower > 24) return fail("ivfc: bad block size");

    // allocation table
    uint64_t fatOff, fatSize;
    if (le32(&h[0x104]) < 0x50000) {
        fatOff = layoutField(h, F_FAT_OFF);
        fatSize = layoutField(h, F_FAT_SIZE);
    } else {   // since version 5 the FAT has its own IVFC: its data level is level 2
        const uint8_t* fl = &h[FAT_IVFC + 0x10];
        fatOff = le64(fl + 0x18 * 2);
        fatSize = le64(fl + 0x18 * 2 + 8);
    }
    if (fatSize > 0x400000) return fail("allocation table too big");
    m_fat.assign((size_t)fatSize, 0);
    if (fatSize && !readMetaRemap(fatOff, m_fat.data(), m_fat.size())) return false;
    m_fatBlocks = le32(&h[FATH + 0x10]);
    m_blockSize = le64(&h[SAVE + 0x10]);
    if (m_blockSize < 0x200 || m_blockSize > 0x100000) return fail("save: bad block size");
    return loadTables();
}

// ---------------------------------------------------------------- file tables
// Directory and file tables: lists of 0x60-byte entries {parent, name[0x40], next sibling, value, next}.
// Entry 0 = free list head (its bytes 4..8 = capacity), entry 1 = used list head. The root directory has
// parent 0 and an empty name.
namespace {
struct RawEntry { uint32_t index, parent; std::string name; uint32_t start; uint64_t size; };
}

static bool readTable(std::vector<RawEntry>& out, std::string& err, const std::function<bool(uint64_t, void*, size_t)>& rd) {
    uint8_t e[0x60];
    if (!rd(0, e, sizeof(e))) { err = "file table unreadable"; return false; }
    uint32_t capacity = le32(e + 4);
    if (!rd(0x60, e, sizeof(e))) { err = "file table unreadable"; return false; }
    uint32_t idx = le32(e + 0x5C);
    for (uint32_t guard = 0; idx; guard++) {
        if (idx > capacity || guard > capacity) { err = "file table: bad index"; return false; }
        if (!rd((uint64_t)idx * 0x60, e, sizeof(e))) { err = "file table unreadable"; return false; }
        RawEntry r;
        r.index = idx;
        r.parent = le32(e);
        r.name.assign((const char*)e + 4, strnlen((const char*)e + 4, 0x40));
        r.start = le32(e + 0x48);
        r.size = le64(e + 0x4C);
        out.push_back(r);
        idx = le32(e + 0x5C);
    }
    return true;
}

bool Image::loadTables() {
    uint32_t dirStart = le32(&m_hdr[FATH + 0x28]), fileStart = le32(&m_hdr[FATH + 0x2C]);
    std::vector<RawEntry> dirs, files;
    std::string err;
    if (!readTable(dirs, err, [&](uint64_t o, void* b, size_t l) { return readChain(dirStart, o, b, l); }) ||
        !readTable(files, err, [&](uint64_t o, void* b, size_t l) { return readChain(fileStart, o, b, l); }))
        return fail(m_err.empty() ? err : err + " (" + m_err + ")");
    // full path of each directory (root = parent 0 + empty name)
    auto dirPath = [&](uint32_t index, std::string& path) -> bool {
        path.clear();
        for (int depth = 0; depth < 64; depth++) {
            const RawEntry* d = nullptr;
            for (const RawEntry& r : dirs) if (r.index == index) { d = &r; break; }
            if (!d) return false;
            if (d->parent == 0 && d->name.empty()) return true;   // root
            path = d->name + (path.empty() ? "" : "/" + path);
            index = d->parent;
        }
        return false;
    };
    for (const RawEntry& f : files) {
        FileInfo fi;
        std::string dir;
        if (!dirPath(f.parent, dir)) continue;
        fi.path = dir.empty() ? f.name : dir + "/" + f.name;
        fi.startBlock = f.start;
        fi.size = f.size;
        m_files.push_back(fi);
    }
    m_segStart = ~0u;
    return true;
}

void Image::close() {
    m_err.clear(); m_commit.clear(); m_hdr.clear(); m_dataRemap = Remap(); m_metaRemap = Remap();
    m_l1.clear(); m_journalMap.clear(); m_fat.clear(); m_files.clear();
    for (Chunk& c : m_cache) { c.off = ~0ULL; c.age = 0; std::vector<uint8_t>().swap(c.data); }
    m_segStart = ~0u; m_segs.clear();
}

const FileInfo* Image::find(const std::string& path) const {
    std::string p = path;
    while (!p.empty() && p[0] == '/') p.erase(0, 1);
    for (const FileInfo& f : m_files) if (f.path == p) return &f;
    return nullptr;
}

bool Image::readFile(const FileInfo& f, uint64_t off, void* buf, size_t len) {
    if (off > f.size || len > f.size - off) return fail("read past the end of " + f.path);
    if (!len) return true;
    return readChain(f.startBlock, off, buf, len);
}

// ---------------------------------------------------------------- SaveSource
bool ImageSaveSource::open(uint64_t) {
    m_open = m_img.open(m_read);
    m_err = m_open ? "" : "image: " + m_img.error();
    m_commit = m_open ? m_img.commit() : std::string();
    return m_open;
}

bool ImageSaveSource::fileSize(const std::string& rel, uint64_t& size) {
    const FileInfo* f = m_open ? m_img.find(rel) : nullptr;
    if (!f) return false;
    size = f->size;
    return true;
}

bool ImageSaveSource::readAt(const std::string& rel, uint64_t off, void* buf, size_t len) {
    const FileInfo* f = m_open ? m_img.find(rel) : nullptr;
    return f && m_img.readFile(*f, off, buf, len);
}

bool ImageSaveSource::readAll(const std::string& rel, std::vector<uint8_t>& out) {
    const FileInfo* f = m_open ? m_img.find(rel) : nullptr;
    if (!f) return false;
    if (m_memory && f->size + 64 * 1024 > m_memory()) return false;
    out.resize((size_t)f->size);
    if (m_img.readFile(*f, 0, out.data(), out.size())) return true;
    out.clear();
    return false;
}

}  // namespace saveimg
