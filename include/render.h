// Tiny software renderer: anti-aliased shapes, TrueType text (system font), icons.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;   // packed like the framebuffer (RGBA bytes)
    bool valid() const { return w > 0; }
};

// Colors are given as 0xAARRGGBB.
class Canvas {
public:
    uint32_t* px = nullptr;
    int w = 1280, h = 720, stride = 1280;

    void setClip(int x, int y, int cw, int ch);
    void resetClip() { cx0 = 0; cy0 = 0; cx1 = w; cy1 = h; }
    void clear(uint32_t c);
    void fillRect(int x, int y, int rw, int rh, uint32_t c);
    void gradientV(int x, int y, int rw, int rh, uint32_t top, uint32_t bottom);
    void fillRound(float x, float y, float rw, float rh, float r, uint32_t c);
    void fillCircle(float cx, float cy, float r, uint32_t c);
    void ring(float cx, float cy, float r, float thick, uint32_t c);
    void line(float x0, float y0, float x1, float y1, float thick, uint32_t c);
    void blit(const Image& img, int x, int y, uint8_t alpha = 255, bool grayscale = false);
    void blitRounded(const Image& img, int x, int y, float radius, bool grayscale = false);
    void blend(int x, int y, uint32_t c, float cov);  // c = 0xAARRGGBB
private:
    int cx0 = 0, cy0 = 0, cx1 = 1280, cy1 = 720;
};

class Font {
public:
    bool load(const uint8_t* data, size_t size, const uint8_t* extData = nullptr, size_t extSize = 0);
    // Draws text with its top-left at (x, y). Returns the advance width.
    int draw(Canvas& c, int x, int y, int px, const std::string& text, uint32_t color, int maxWidth = -1);
    int measure(int px, const std::string& text);
    std::string ellipsize(int px, const std::string& text, int maxWidth);
private:
    struct Glyph { int w = 0, h = 0, xoff = 0, yoff = 0; float advance = 0; std::vector<uint8_t> bmp; };
    struct Face { void* info = nullptr; bool ok = false; };
    Face faces[2];
    std::unordered_map<uint64_t, Glyph> cache;
    const Glyph& glyph(int px, uint32_t cp);
    int ascent(int px);
};

bool decodeIcon(const std::vector<uint8_t>& jpeg, int size, Image& out);
bool decodeCompressedTitles(const uint8_t* langData, size_t langSize, std::vector<std::string>& names);
uint32_t decodeUtf8(const std::string& s, size_t& i);
