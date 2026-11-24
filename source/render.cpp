#include "render.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_SUPPORT_ZLIB
#define STBI_NO_STDIO
#include "stb_image.h"

// Framebuffer pixel = bytes R,G,B,A  ->  little-endian u32 = A<<24 | B<<16 | G<<8 | R
static inline uint32_t pack(uint32_t argb) {
    uint32_t a = argb >> 24, r = (argb >> 16) & 255, g = (argb >> 8) & 255, b = argb & 255;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

void Canvas::setClip(int x, int y, int cw, int ch) {
    cx0 = std::max(0, x); cy0 = std::max(0, y);
    cx1 = std::min(w, x + cw); cy1 = std::min(h, y + ch);
}

void Canvas::clear(uint32_t c) {
    uint32_t p = pack(c | 0xFF000000u);
    for (int y = 0; y < h; y++) std::fill(px + y * stride, px + y * stride + w, p);
}

void Canvas::blend(int x, int y, uint32_t c, float cov) {
    if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1 || cov <= 0) return;
    float a = ((c >> 24) / 255.0f) * std::min(cov, 1.0f);
    if (a <= 0.002f) return;
    uint32_t& d = px[y * stride + x];
    uint32_t dr = d & 255, dg = (d >> 8) & 255, db = (d >> 16) & 255;
    uint32_t sr = (c >> 16) & 255, sg = (c >> 8) & 255, sb = c & 255;
    uint32_t r = (uint32_t)(dr + (sr - (float)dr) * a);
    uint32_t g = (uint32_t)(dg + (sg - (float)dg) * a);
    uint32_t b = (uint32_t)(db + (sb - (float)db) * a);
    d = 0xFF000000u | (b << 16) | (g << 8) | r;
}

void Canvas::fillRect(int x, int y, int rw, int rh, uint32_t c) {
    int x0 = std::max(x, cx0), y0 = std::max(y, cy0), x1 = std::min(x + rw, cx1), y1 = std::min(y + rh, cy1);
    if ((c >> 24) == 255) {
        uint32_t p = pack(c);
        for (int yy = y0; yy < y1; yy++) std::fill(px + yy * stride + x0, px + yy * stride + std::max(x0, x1), p);
        return;
    }
    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) blend(xx, yy, c, 1);
}

static uint32_t lerpColor(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        float ca = (a >> s) & 255, cb = (b >> s) & 255;
        out |= ((uint32_t)(ca + (cb - ca) * t) & 255) << s;
    }
    return out;
}

void Canvas::gradientV(int x, int y, int rw, int rh, uint32_t top, uint32_t bottom) {
    for (int i = 0; i < rh; i++) fillRect(x, y + i, rw, 1, lerpColor(top, bottom, rh > 1 ? (float)i / (rh - 1) : 0));
}

void Canvas::fillRound(float x, float y, float rw, float rh, float r, uint32_t c) {
    r = std::min(r, std::min(rw, rh) / 2);
    int x0 = std::max((int)floorf(x), cx0), y0 = std::max((int)floorf(y), cy0);
    int x1 = std::min((int)ceilf(x + rw), cx1), y1 = std::min((int)ceilf(y + rh), cy1);
    bool opaque = (c >> 24) == 255;
    uint32_t packed = pack(c);
    for (int yy = y0; yy < y1; yy++) {
        float py = yy + 0.5f;
        float qy = std::max(std::max(y + r - py, py - (y + rh - r)), 0.0f);
        float ey = std::min(py - y, y + rh - py);
        // Fast path: rows away from the corners are a solid span with 1px AA edges.
        if (qy <= 0 && ey >= 1.0f && opaque) {
            int sx0 = std::max(x0, (int)ceilf(x + 0.5f)), sx1 = std::min(x1, (int)floorf(x + rw - 0.5f));
            for (int xx = x0; xx < sx0; xx++) blend(xx, yy, c, std::min(xx + 1.0f - x, 1.0f));
            if (sx1 > sx0) std::fill(px + yy * stride + sx0, px + yy * stride + sx1, packed);
            for (int xx = std::max(sx1, sx0); xx < x1; xx++) blend(xx, yy, c, std::min(x + rw - xx, 1.0f));
            continue;
        }
        for (int xx = x0; xx < x1; xx++) {
            float pxl = xx + 0.5f;
            float qx = std::max(std::max(x + r - pxl, pxl - (x + rw - r)), 0.0f);
            float cov;
            if (qx > 0 && qy > 0) cov = 0.5f - (sqrtf(qx * qx + qy * qy) - r);   // corner: distance to arc
            else cov = std::min(std::min(std::min(pxl - x, x + rw - pxl), ey) + 0.5f, 1.0f);
            if (cov > 0) blend(xx, yy, c, cov);
        }
    }
}

void Canvas::fillCircle(float cx, float cy, float r, uint32_t c) {
    int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1), y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
        blend(x, y, c, r - d + 0.5f);
    }
}

void Canvas::ring(float cx, float cy, float r, float thick, uint32_t c) {
    int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1), y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
        float cov = std::min(r - d + 0.5f, d - (r - thick) + 0.5f);
        blend(x, y, c, cov);
    }
}

void Canvas::line(float x0, float y0, float x1, float y1, float thick, uint32_t c) {
    float hw = thick / 2;
    int bx0 = (int)floorf(std::min(x0, x1) - hw - 1), bx1 = (int)ceilf(std::max(x0, x1) + hw + 1);
    int by0 = (int)floorf(std::min(y0, y1) - hw - 1), by1 = (int)ceilf(std::max(y0, y1) + hw + 1);
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int y = by0; y < by1; y++) for (int x = bx0; x < bx1; x++) {
        float px_ = x + 0.5f, py_ = y + 0.5f;
        float t = len2 > 0 ? ((px_ - x0) * dx + (py_ - y0) * dy) / len2 : 0;
        t = std::max(0.0f, std::min(1.0f, t));
        float ex = x0 + t * dx - px_, ey = y0 + t * dy - py_;
        blend(x, y, c, hw - sqrtf(ex * ex + ey * ey) + 0.5f);
    }
}

static inline uint32_t toArgb(uint32_t p, bool gray) {
    uint32_t r = p & 255, g = (p >> 8) & 255, b = (p >> 16) & 255;
    if (gray) { uint32_t l = (r * 77 + g * 150 + b * 29) >> 8; l = l / 2 + 40; r = g = b = l; }
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void Canvas::blit(const Image& img, int x, int y, uint8_t alpha, bool gray) {
    for (int j = 0; j < img.h; j++) for (int i = 0; i < img.w; i++)
        blend(x + i, y + j, toArgb(img.px[j * img.w + i], gray), alpha / 255.0f);
}

void Canvas::blitAlpha(const Image& img, int x, int y) {
    for (int j = 0; j < img.h; j++) for (int i = 0; i < img.w; i++) {
        uint32_t p = img.px[j * img.w + i];
        if (p >> 24) blend(x + i, y + j, toArgb(p, false), (p >> 24) / 255.0f);
    }
}

void Canvas::blitRounded(const Image& img, int x, int y, float radius, bool gray) {
    for (int j = 0; j < img.h; j++) for (int i = 0; i < img.w; i++) {
        float px_ = i + 0.5f, py_ = j + 0.5f;
        float qx = std::max(std::max(radius - px_, px_ - (img.w - radius)), 0.0f);
        float qy = std::max(std::max(radius - py_, py_ - (img.h - radius)), 0.0f);
        float cov = (qx > 0 && qy > 0) ? radius - sqrtf(qx * qx + qy * qy) + 0.5f : 1.0f;
        blend(x + i, y + j, toArgb(img.px[j * img.w + i], gray), cov);
    }
}

// ---------------------------------------------------------------- text
uint32_t decodeUtf8(const std::string& s, size_t& i) {
    unsigned char c = s[i++];
    if (c < 0x80) return c;
    int n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    uint32_t cp = c & (0x3F >> n);
    for (int k = 0; k < n && i < s.size(); k++) cp = (cp << 6) | (s[i++] & 0x3F);
    return cp;
}

bool Font::load(const uint8_t* data, size_t, const uint8_t* ext, size_t) {
    const uint8_t* srcs[2] = {data, ext};
    for (int i = 0; i < 2; i++) {
        if (!srcs[i]) continue;
        stbtt_fontinfo* fi = new stbtt_fontinfo;
        if (stbtt_InitFont(fi, srcs[i], stbtt_GetFontOffsetForIndex(srcs[i], 0))) { faces[i].info = fi; faces[i].ok = true; }
        else delete fi;
    }
    return faces[0].ok;
}

int Font::ascent(int px) {
    stbtt_fontinfo* fi = (stbtt_fontinfo*)faces[0].info;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(fi, &asc, &desc, &gap);
    return (int)lroundf(asc * stbtt_ScaleForMappingEmToPixels(fi, (float)px));
}

const Font::Glyph& Font::glyph(int px, uint32_t cp) {
    uint64_t key = ((uint64_t)px << 32) | cp;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Glyph g;
    for (int f = 0; f < 2; f++) {
        if (!faces[f].ok) continue;
        stbtt_fontinfo* fi = (stbtt_fontinfo*)faces[f].info;
        int idx = stbtt_FindGlyphIndex(fi, (int)cp);
        if (idx == 0 && f == 0 && faces[1].ok) continue;  // try the extension font
        float scale = stbtt_ScaleForMappingEmToPixels(fi, (float)px);
        int adv, lsb;
        stbtt_GetGlyphHMetrics(fi, idx, &adv, &lsb);
        g.advance = adv * scale;
        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(fi, idx, scale, scale, &x0, &y0, &x1, &y1);
        g.w = x1 - x0; g.h = y1 - y0; g.xoff = x0; g.yoff = y0;
        if (g.w > 0 && g.h > 0) {
            g.bmp.resize(g.w * g.h);
            stbtt_MakeGlyphBitmap(fi, g.bmp.data(), g.w, g.h, g.w, scale, scale, idx);
        }
        break;
    }
    return cache.emplace(key, std::move(g)).first->second;
}

int Font::measure(int px, const std::string& text) {
    float x = 0;
    for (size_t i = 0; i < text.size();) x += glyph(px, decodeUtf8(text, i)).advance;
    return (int)ceilf(x);
}

std::string Font::ellipsize(int px, const std::string& text, int maxWidth) {
    if (maxWidth < 0 || measure(px, text) <= maxWidth) return text;
    int ell = measure(px, "...");
    float x = 0;
    size_t i = 0, cut = 0;
    while (i < text.size()) {
        size_t start = i;
        float a = glyph(px, decodeUtf8(text, i)).advance;
        if (x + a + ell > maxWidth) { cut = start; break; }
        x += a;
        cut = i;
    }
    return text.substr(0, cut) + "...";
}

int Font::draw(Canvas& c, int x, int y, int px, const std::string& text0, uint32_t color, int maxWidth) {
    if (!faces[0].ok) return 0;
    std::string text = ellipsize(px, text0, maxWidth);
    int base = y + ascent(px);
    float pen = (float)x;
    for (size_t i = 0; i < text.size();) {
        const Glyph& g = glyph(px, decodeUtf8(text, i));
        int gx = (int)lroundf(pen) + g.xoff, gy = base + g.yoff;
        for (int j = 0; j < g.h; j++) for (int k = 0; k < g.w; k++) {
            uint8_t a = g.bmp[j * g.w + k];
            if (a) c.blend(gx + k, gy + j, color, a / 255.0f);
        }
        pen += g.advance;
    }
    return (int)(pen - x);
}

// ---------------------------------------------------------------- compressed NACP titles
// [21.0.0+] games may store 32 language entries (0x300 bytes each: name 0x200, author 0x100)
// as raw DEFLATE: u16 size followed by the data.
bool decodeCompressedTitles(const uint8_t* langData, size_t langSize, std::vector<std::string>& names) {
    names.clear();
    if (langSize < 2) return false;
    uint16_t sz = (uint16_t)(langData[0] | (langData[1] << 8));
    if (sz == 0 || sz > langSize - 2) return false;
    std::vector<char> out(0x300 * 32);
    int n = stbi_zlib_decode_noheader_buffer(out.data(), (int)out.size(), (const char*)langData + 2, sz);
    if (n <= 0) return false;
    for (int i = 0; i + 0x300 <= n; i += 0x300) names.push_back(std::string(&out[i], strnlen(&out[i], 0x200)));
    return !names.empty();
}

// ---------------------------------------------------------------- icons
bool decodeIcon(const std::vector<uint8_t>& jpeg, int size, Image& out) {
    if (jpeg.empty()) return false;
    int w, h, n;
    unsigned char* d = stbi_load_from_memory(jpeg.data(), (int)jpeg.size(), &w, &h, &n, 3);
    if (!d) return false;
    out.w = out.h = size;
    out.px.assign(size * size, 0);
    // Box filter downscale
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        int sx0 = x * w / size, sx1 = std::max(sx0 + 1, (x + 1) * w / size);
        int sy0 = y * h / size, sy1 = std::max(sy0 + 1, (y + 1) * h / size);
        uint32_t r = 0, g = 0, b = 0, cnt = 0;
        for (int sy = sy0; sy < sy1; sy++) for (int sx = sx0; sx < sx1; sx++) {
            const unsigned char* p = d + (sy * w + sx) * 3;
            r += p[0]; g += p[1]; b += p[2]; cnt++;
        }
        out.px[y * size + x] = 0xFF000000u | ((b / cnt) << 16) | ((g / cnt) << 8) | (r / cnt);
    }
    stbi_image_free(d);
    return true;
}
