#include "flagscan.h"
#include <algorithm>
#include <cstring>

static const size_t CHUNK = 0x10000;   // 64 KiB per read (small footprint)
static const size_t OVERLAP = 0x20;    // bytes kept from the previous chunk (object header before the hash)
static uint8_t g_buf[CHUNK + OVERLAP];

static inline uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint64_t rd64(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }

void FlagScanner::setWanted(const std::vector<uint32_t>& hashes) {
    m_wanted.clear();
    std::fill(m_bloom.begin(), m_bloom.end(), 0);
    for (uint32_t h : hashes) {
        m_wanted.insert(h);
        m_bloom[(h & 0xFFFF) >> 3] |= 1 << (h & 7);
    }
}

bool FlagScanner::checkObject(const uint8_t* obj, FlagKind& kind) const {
    uint64_t vt = rd64(obj);
    if (vt < m_mainBase || vt >= m_mainBase + m_mainSize || (vt & 7)) return false;
    uint8_t type = obj[8];
    if (kind == FK_BOOL) {
        if (type != 0) return false;
        if (obj[0xB] > 1 || obj[0xA] > 1) return false;
        return true;
    }
    if (type == 1) {
        int32_t init, mn, mx, v;
        memcpy(&init, obj + 0x0C, 4); memcpy(&mn, obj + 0x10, 4); memcpy(&mx, obj + 0x14, 4); memcpy(&v, obj + 0x18, 4);
        if (mn > mx) return false;
        kind = FK_S32;
        return true;
    }
    if (type == 2) {
        float mn, mx;
        memcpy(&mn, obj + 0x10, 4); memcpy(&mx, obj + 0x14, 4);
        if (!(mn <= mx)) return false;   // also rejects NaN
        kind = FK_F32;
        return true;
    }
    return false;
}

ScanStats FlagScanner::scan(MemReader& mem, const std::vector<MemRegion>& regions, void (*yield)()) {
    ScanStats st;
    st.hashesWanted = (uint32_t)m_wanted.size();
    m_hits.clear();
    m_byHash.clear();
    for (const MemRegion& r : regions) {
        size_t carry = 0;   // bytes of the previous chunk kept at the start of g_buf
        for (uint64_t off = 0; off < r.size; off += CHUNK) {
            size_t len = (size_t)std::min<uint64_t>(CHUNK, r.size - off);
            uint64_t addr = r.base + off;
            if (!mem.read(addr, g_buf + carry, len)) {
                st.readErrors++;
                carry = 0;
                continue;
            }
            st.bytesScanned += len;
            uint64_t bufAddr = addr - carry;   // absolute address of g_buf[0]
            size_t total = carry + len;
            // Flag objects are 8-aligned and the hash sits at +0x0C or +0x1C: absolute addr % 8 == 4.
            size_t start = (size_t)((4 - (bufAddr & 7) + 8) & 7);
            for (size_t i = start; i + 4 <= total; i += 8) {
                uint32_t h = rd32(g_buf + i);
                if (!(m_bloom[(h & 0xFFFF) >> 3] & (1 << (h & 7)))) continue;
                if (!m_wanted.count(h)) continue;
                st.candidates++;
                bool ok = false;
                FlagKind kind = FK_BOOL;
                if (i >= 0x0C && checkObject(g_buf + i - 0x0C, kind)) {
                    m_byHash[h].push_back(m_hits.size());
                    m_hits.push_back({h, bufAddr + i - 0x0C, FK_BOOL});
                    ok = true;
                }
                kind = FK_S32;
                if (!ok && i >= 0x1C && checkObject(g_buf + i - 0x1C, kind)) {
                    m_byHash[h].push_back(m_hits.size());
                    m_hits.push_back({h, bufAddr + i - 0x1C, kind});
                    ok = true;
                }
                if (!ok) st.rejected++;
            }
            // keep the tail so objects straddling the chunk boundary are still seen
            carry = std::min(OVERLAP, total);
            memmove(g_buf, g_buf + total - carry, carry);
            if (yield) yield();
        }
    }
    st.hashesFound = (uint32_t)m_byHash.size();
    return st;
}

size_t FlagScanner::foundCount() const { return m_byHash.size(); }

bool FlagScanner::value(MemReader& mem, uint32_t hash, double& out, bool& stale) {
    stale = false;
    auto it = m_byHash.find(hash);
    if (it == m_byHash.end()) return false;
    bool any = false;
    double best = 0;
    for (size_t idx : it->second) {
        const FlagHit& f = m_hits[idx];
        uint8_t obj[0x28];
        size_t sz = f.kind == FK_BOOL ? 0x18 : 0x28;
        if (!mem.read(f.objAddr, obj, sz)) continue;
        uint32_t hOff = f.kind == FK_BOOL ? 0x0C : 0x1C;
        FlagKind k = f.kind == FK_BOOL ? FK_BOOL : FK_S32;
        if (rd32(obj + hOff) != hash || !checkObject(obj, k)) continue;   // object freed/moved
        double v;
        if (f.kind == FK_BOOL) v = obj[0xB] & 1;
        else if (f.kind == FK_S32) { int32_t s; memcpy(&s, obj + 0x18, 4); v = s; }
        else { float fl; memcpy(&fl, obj + 0x18, 4); v = fl; }
        best = any ? std::max(best, v) : v;
        any = true;
    }
    if (!any) { stale = true; return false; }
    out = best;
    return true;
}
