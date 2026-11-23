// Read-only FAT32 over raw sectors, see include/rawfat.h.
#include "rawfat.h"
#include <algorithm>
#include <cstring>

namespace rawfat {

static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

bool File::read(const ReadFn& rd, uint64_t off, void* buf, size_t len) const {
    if (off > size || len > size - off) return false;
    uint8_t* out = (uint8_t*)buf;
    uint64_t pos = 0;   // file offset of the current run
    for (const auto& r : runs) {
        while (len && off < pos + r.second) {
            uint64_t in = off - pos;
            size_t n = (size_t)std::min<uint64_t>(len, r.second - in);
            if (!rd(r.first + in, out, n)) return false;
            out += n; off += n; len -= n;
        }
        if (!len) return true;
        pos += r.second;
    }
    return len == 0;
}

bool Volume::mount(ReadFn read) {
    m_read = std::move(read);
    m_fatSecOff = ~0ULL;
    uint8_t bs[512];
    if (!m_read(0, bs, sizeof(bs))) return fail("boot sector unreadable");
    if (bs[0x1FE] != 0x55 || bs[0x1FF] != 0xAA || memcmp(bs + 0x52, "FAT32", 5) != 0) return fail("not FAT32");
    m_bps = le16(bs + 0x0B);
    uint32_t spc = bs[0x0D], reserved = le16(bs + 0x0E), fats = bs[0x10], fatSize = le32(bs + 0x24);
    uint32_t total = le16(bs + 0x13) ? le16(bs + 0x13) : le32(bs + 0x20);
    if ((m_bps != 512 && m_bps != 1024 && m_bps != 2048 && m_bps != 4096) || !spc || (spc & (spc - 1)) || !fats || !fatSize)
        return fail("bad FAT32 boot sector");
    m_cluster = m_bps * spc;
    m_root = le32(bs + 0x2C);
    m_fatOff = (uint64_t)reserved * m_bps;
    uint64_t dataSec = (uint64_t)reserved + (uint64_t)fats * fatSize;
    m_dataOff = dataSec * m_bps;
    m_clusters = total > dataSec ? (uint32_t)((total - dataSec) / spc) : 0;
    return true;
}

bool Volume::nextCluster(uint32_t c, uint32_t& next) {
    uint64_t byte = m_fatOff + (uint64_t)c * 4;
    uint64_t sec = byte / m_bps * m_bps;
    if (sec != m_fatSecOff) {
        m_fatSec.resize(m_bps);
        if (!m_read(sec, m_fatSec.data(), m_bps)) { m_fatSecOff = ~0ULL; return fail("FAT unreadable"); }
        m_fatSecOff = sec;
    }
    next = le32(&m_fatSec[byte - sec]) & 0x0FFFFFFF;
    return true;
}

bool Volume::chain(uint32_t first, std::vector<uint32_t>& out, uint32_t maxClusters) {
    out.clear();
    uint32_t c = first;
    while (c >= 2 && c < 0x0FFFFFF8) {
        if (c - 2 >= m_clusters || out.size() >= maxClusters) return fail("bad cluster chain");
        out.push_back(c);
        if (!nextCluster(c, c)) return false;
    }
    return true;
}

// Directory entries: 32 bytes. Long names come as 0x0F entries before the short entry, last part first.
bool Volume::findIn(uint32_t dirCluster, const std::string& name, Entry& out) {
    std::vector<uint32_t> clusters;
    if (!chain(dirCluster, clusters, 1u << 16)) return false;
    const std::string want = lower(name);
    std::vector<uint8_t> buf(m_cluster);
    std::u16string lfn;
    bool haveLfn = false;
    for (uint32_t c : clusters) {
        if (!m_read(m_dataOff + (uint64_t)(c - 2) * m_cluster, buf.data(), m_cluster)) return fail("directory unreadable");
        for (uint32_t o = 0; o + 32 <= m_cluster; o += 32) {
            const uint8_t* e = &buf[o];
            if (e[0] == 0x00) return fail(name + " not found");
            if (e[0] == 0xE5) { haveLfn = false; continue; }
            uint8_t attr = e[0x0B];
            if (attr == 0x0F) {
                uint8_t seq = e[0], ord = seq & 0x1F;
                if (!ord) { haveLfn = false; continue; }
                if (seq & 0x40) { lfn.assign((size_t)ord * 13, u'\0'); haveLfn = true; }
                if (!haveLfn || (size_t)ord * 13 > lfn.size()) { haveLfn = false; continue; }
                static const int pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
                for (int k = 0; k < 13; k++) lfn[(ord - 1) * 13 + k] = (char16_t)le16(e + pos[k]);
                continue;
            }
            if (attr & 0x08) { haveLfn = false; continue; }   // volume label
            std::string n;
            if (haveLfn) {
                for (char16_t ch : lfn) { if (ch == 0 || ch == 0xFFFF) break; n += ch < 0x80 ? (char)ch : '?'; }
            } else {
                std::string base((const char*)e, 8), ext((const char*)e + 8, 3);
                while (!base.empty() && base.back() == ' ') base.pop_back();
                while (!ext.empty() && ext.back() == ' ') ext.pop_back();
                n = ext.empty() ? base : base + "." + ext;
            }
            haveLfn = false;
            if (lower(n) != want) continue;
            out.name = n;
            out.cluster = ((uint32_t)le16(e + 0x14) << 16) | le16(e + 0x1A);
            out.size = le32(e + 0x1C);
            out.dir = (attr & 0x10) != 0;
            return true;
        }
    }
    return fail(name + " not found");
}

bool Volume::open(const std::string& path, File& out) {
    if (!m_cluster) return fail("not mounted");
    uint32_t dir = m_root;
    size_t i = 0;
    Entry e;
    bool isDir = true;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') i++;
        size_t j = path.find('/', i);
        std::string part = path.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (part.empty()) break;
        if (!isDir) return fail(path + ": not a directory");
        if (!findIn(dir, part, e)) return false;
        dir = e.cluster;
        isDir = e.dir;
        i = j == std::string::npos ? path.size() : j;
    }
    if (isDir) return fail(path + " is a directory");
    std::vector<uint32_t> clusters;
    uint32_t need = (uint32_t)((e.size + m_cluster - 1) / m_cluster);
    if (!chain(e.cluster, clusters, need + 1)) return false;
    if (clusters.size() < need) return fail("cluster chain shorter than the file");
    out = File();
    out.size = e.size;
    for (uint32_t k = 0; k < need; k++) {
        uint64_t off = m_dataOff + (uint64_t)(clusters[k] - 2) * m_cluster;
        if (!out.runs.empty() && out.runs.back().first + out.runs.back().second == off) out.runs.back().second += m_cluster;
        else out.runs.push_back({off, m_cluster});
    }
    // the last cluster may be partly used: trim the total to the file size
    uint64_t total = (uint64_t)need * m_cluster;
    if (total > e.size && !out.runs.empty()) out.runs.back().second -= total - e.size;
    return true;
}

}  // namespace rawfat
