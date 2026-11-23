// Reader for the Switch's save data container (the raw image stored in NAND at User:/save/<SaveDataId>),
// so a save can be read while its game is running: the game keeps the save mounted (locked), but the
// last committed state is in that image. Read-only; no keys needed (hashes and MACs are not checked,
// except the header hash used to pick the valid header copy).
// Layout from hactool's save.c (DISF header, remap, hierarchical duplex, journal, IVFC, FAT, file tables).
// Shared by the background module (live save watch) and the host tests; nothing in here touches Switch services.
#pragma once
#include "rules.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace saveimg {

// Reads len bytes of the image at off. false = read error.
using ReadFn = std::function<bool(uint64_t off, void* buf, size_t len)>;

// Identity of the last commit: hash of the header copy in use ("" = no valid header).
// Changes every time the game saves (commits), so polling it is cheap (one 32 KB read).
std::string commitId(const ReadFn& read);

struct FileInfo {
    std::string path;     // "dir/file.bin" (no leading slash)
    uint32_t startBlock = 0;
    uint64_t size = 0;
};

class Image {
public:
    // Parses the header and the small tables. false + error() if it isn't a usable save image.
    bool open(ReadFn read);
    void close();
    const std::string& error() const { return m_err; }
    const std::string& commit() const { return m_commit; }   // commitId() of what open() parsed
    const std::vector<FileInfo>& files() const { return m_files; }
    const FileInfo* find(const std::string& path) const;
    bool readFile(const FileInfo& f, uint64_t off, void* buf, size_t len);

private:
    struct RemapEntry { uint64_t virt, phys, size; };
    struct Remap { std::vector<RemapEntry> entries; uint64_t base = 0; };
    bool readBase(uint64_t off, void* buf, size_t len);   // through a small chunk cache
    bool readDataRemap(uint64_t off, void* buf, size_t len);
    bool readDuplex(uint64_t off, void* buf, size_t len);  // duplex data layer (under the meta remap)
    bool readMetaRemap(uint64_t off, void* buf, size_t len);
    bool readJournal(uint64_t off, void* buf, size_t len);
    bool readCoreData(uint64_t off, void* buf, size_t len); // IVFC data level (zero hash -> zeros)
    bool readChain(uint32_t startBlock, uint64_t off, void* buf, size_t len);
    bool chainSegments(uint32_t startBlock, std::vector<std::pair<uint32_t, uint32_t>>& segs);
    bool loadRemap(Remap& r, uint64_t entriesOff, uint32_t count, uint64_t base);
    bool loadTables();
    bool fail(const std::string& e) { m_err = e; return false; }

    ReadFn m_read;
    std::string m_err, m_commit;
    std::vector<uint8_t> m_hdr;                 // header copy in use (0x4000)
    Remap m_dataRemap, m_metaRemap;
    std::vector<uint8_t> m_l1;                  // resolved duplex L1 bitmap
    uint32_t m_l1Power = 0, m_dataPower = 0;
    uint64_t m_duplexA = 0, m_duplexB = 0;
    std::vector<uint32_t> m_journalMap;          // virtual block -> physical block
    uint64_t m_journalBlock = 0, m_journalDataOff = 0;
    uint64_t m_ivfcDataOff = 0, m_ivfcDataSize = 0, m_ivfcHashOff = 0;
    uint32_t m_ivfcPower = 0;
    std::vector<uint8_t> m_fat;                  // allocation table
    uint32_t m_fatBlocks = 0;
    uint64_t m_blockSize = 0;
    std::vector<FileInfo> m_files;
    // chunk cache of the image itself (the module reads NAND sector by sector: keep reads big and few)
    struct Chunk { uint64_t off = ~0ULL; uint32_t age = 0; std::vector<uint8_t> data; };
    Chunk m_cache[4];
    uint32_t m_clock = 0;
    // cache of the last chain walked (rules read many small values of the same file)
    uint32_t m_segStart = ~0u;
    std::vector<std::pair<uint32_t, uint32_t>> m_segs;
};

// SaveSource over an image: files of the save by their path inside it.
class ImageSaveSource : public SaveSource {
public:
    explicit ImageSaveSource(ReadFn read, std::function<size_t()> memory = nullptr)
        : m_read(std::move(read)), m_memory(std::move(memory)) {}
    bool open(uint64_t titleId) override;
    void close() override { m_img.close(); m_open = false; }
    std::string lastError() override { return m_err; }
    bool readAll(const std::string& rel, std::vector<uint8_t>& out) override;
    bool streaming() const override { return true; }
    bool fileSize(const std::string& rel, uint64_t& size) override;
    bool readAt(const std::string& rel, uint64_t off, void* buf, size_t len) override;
    size_t memoryAvailable() override { return m_memory ? m_memory() : (size_t)-1; }
    // commitId() of the state that was read (to check that the game didn't save meanwhile)
    const std::string& commit() const { return m_img.commit(); }

private:
    ReadFn m_read;
    std::function<size_t()> m_memory;
    Image m_img;
    bool m_open = false;
    std::string m_err;
};

}  // namespace saveimg
