// SHA-256 (FIPS 180-4), used to check downloaded update files.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(const void* data, size_t len);
    std::string hexDigest();   // finishes the hash (lowercase hex)
private:
    void block(const uint8_t* p);
    uint32_t m_h[8];
    uint8_t m_buf[64];
    size_t m_used = 0;
    uint64_t m_bits = 0;
};
std::string sha256Hex(const void* data, size_t len);
// Hash of a file ("" if it can't be read).
std::string sha256File(const std::string& path, uint64_t* sizeOut = nullptr);
