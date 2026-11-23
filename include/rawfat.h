// Minimal read-only FAT32 reader over raw sectors. The background module uses it on the NAND User partition
// (BIS storage) to find a save image (/save/<SaveDataId>) and read it while its game runs: opening the file
// itself would compete with the game's lock, reading the partition's sectors doesn't touch it.
// Nothing in here touches Switch services; the caller gives a read function.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rawfat {

// Reads len bytes of the partition at off (off and len are multiples of 512).
using ReadFn = std::function<bool(uint64_t off, void* buf, size_t len)>;

struct File {
    uint64_t size = 0;
    std::vector<std::pair<uint64_t, uint64_t>> runs;   // {partition offset, length}: the file's clusters, merged
    // Reads part of the file through the partition read function.
    bool read(const ReadFn& rd, uint64_t off, void* buf, size_t len) const;
};

class Volume {
public:
    bool mount(ReadFn read);                 // checks the FAT32 boot sector
    const std::string& error() const { return m_err; }
    // path like "/save/0000000000000123" (case-insensitive, long names supported)
    bool open(const std::string& path, File& out);
    uint32_t clusterSize() const { return m_cluster; }

private:
    struct Entry { std::string name; uint32_t cluster = 0; uint32_t size = 0; bool dir = false; };
    bool nextCluster(uint32_t c, uint32_t& next);
    bool chain(uint32_t first, std::vector<uint32_t>& out, uint32_t maxClusters);
    bool findIn(uint32_t dirCluster, const std::string& name, Entry& out);
    bool fail(const std::string& e) { m_err = e; return false; }
    ReadFn m_read;
    std::string m_err;
    uint32_t m_bps = 512, m_cluster = 0, m_root = 0, m_clusters = 0;
    uint64_t m_fatOff = 0, m_dataOff = 0;
    std::vector<uint8_t> m_fatSec;           // last FAT sector read
    uint64_t m_fatSecOff = ~0ULL;
};

}  // namespace rawfat
