// Achievement popup renderer: text bar + icon + tier medal, drawn in software into RGBA8.
#include "popup_draw.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

static stbtt_fontinfo g_font;
static bool g_fontOk = false;

bool popupFontInit(const uint8_t* ttf) {
    g_fontOk = ttf && stbtt_InitFont(&g_font, ttf, stbtt_GetFontOffsetForIndex(ttf, 0));
    return g_fontOk;
}

// ---------------------------------------------------------------- pixels (straight alpha "over")
struct Rgba { float r, g, b, a; };
static Rgba hex(uint32_t c, float a = 1.f) {
    return {((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, a};
}

static void blend(PopupImage& im, int x, int y, Rgba c, float cov) {
    if (x < 0 || y < 0 || x >= im.w || y >= im.h || cov <= 0) return;
    uint8_t* p = &im.px[(size_t)(y * im.w + x) * 4];
    float sa = c.a * std::min(cov, 1.f), da = p[3] / 255.f;
    float oa = sa + da * (1 - sa);
    if (oa <= 0) return;
    float k = da * (1 - sa);
    p[0] = (uint8_t)lroundf((c.r * sa + p[0] / 255.f * k) / oa * 255);
    p[1] = (uint8_t)lroundf((c.g * sa + p[1] / 255.f * k) / oa * 255);
    p[2] = (uint8_t)lroundf((c.b * sa + p[2] / 255.f * k) / oa * 255);
    p[3] = (uint8_t)lroundf(oa * 255);
}

static void fillRect(PopupImage& im, float x0, float y0, float x1, float y1, Rgba c) {
    for (int y = (int)floorf(y0); y < (int)ceilf(y1); y++)
        for (int x = (int)floorf(x0); x < (int)ceilf(x1); x++) {
            float cx = std::min(x + 1.f, x1) - std::max((float)x, x0), cy = std::min(y + 1.f, y1) - std::max((float)y, y0);
            blend(im, x, y, c, cx * cy);
        }
}

void popupScaleImage(const uint8_t* src, int sw, int sh, int dw, int dh, PopupImage& out) {
    out.w = dw; out.h = dh;
    out.px.assign((size_t)dw * dh * 4, 0);
    for (int y = 0; y < dh; y++)
        for (int x = 0; x < dw; x++) {
            int sx0 = x * sw / dw, sx1 = std::max(sx0 + 1, (x + 1) * sw / dw);
            int sy0 = y * sh / dh, sy1 = std::max(sy0 + 1, (y + 1) * sh / dh);
            float acc[4] = {0, 0, 0, 0}; int n = 0;
            for (int yy = sy0; yy < sy1 && yy < sh; yy++)
                for (int xx = sx0; xx < sx1 && xx < sw; xx++, n++) {
                    const uint8_t* p = &src[(size_t)(yy * sw + xx) * 4];
                    float a = p[3] / 255.f;   // weight colour by alpha
                    acc[0] += p[0] * a; acc[1] += p[1] * a; acc[2] += p[2] * a; acc[3] += a;
                }
            uint8_t* d = &out.px[(size_t)(y * dw + x) * 4];
            if (n && acc[3] > 0) {
                d[0] = (uint8_t)(acc[0] / acc[3]); d[1] = (uint8_t)(acc[1] / acc[3]); d[2] = (uint8_t)(acc[2] / acc[3]);
                d[3] = (uint8_t)(acc[3] / n * 255);
            }
        }
}

static void drawImage(PopupImage& im, const PopupImage& src, int x0, int y0) {
    for (int y = 0; y < src.h; y++)
        for (int x = 0; x < src.w; x++) {
            const uint8_t* p = &src.px[(size_t)(y * src.w + x) * 4];
            blend(im, x0 + x, y0 + y, {p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, 1.f}, p[3] / 255.f);
        }
}

// ---------------------------------------------------------------- text
static uint32_t nextCp(const char*& s) {
    const uint8_t* u = (const uint8_t*)s;
    uint32_t c = *u++;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (extra) c &= (0x3F >> extra);
    while (extra-- > 0 && (*u & 0xC0) == 0x80) c = (c << 6) | (*u++ & 0x3F);
    s = (const char*)u;
    return c;
}

static float textWidth(const std::string& t, float px) {
    if (!g_fontOk) return 0;
    float sc = stbtt_ScaleForMappingEmToPixels(&g_font, px), w = 0;
    for (const char* s = t.c_str(); *s;) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&g_font, (int)nextCp(s), &adv, &lsb);
        w += adv * sc;
    }
    return w;
}

static std::string ellipsize(const std::string& t, float px, float maxW) {
    if (textWidth(t, px) <= maxW) return t;
    std::vector<size_t> starts;   // codepoint boundaries
    for (const char* s = t.c_str(); *s;) { starts.push_back(s - t.c_str()); nextCp(s); }
    for (size_t n = starts.size(); n > 0; n--) {
        std::string c = t.substr(0, starts[n - 1]);
        while (!c.empty() && c.back() == ' ') c.pop_back();
        c += "...";
        if (textWidth(c, px) <= maxW) return c;
    }
    return "...";
}

// Draws with the top of the em box at y (canvas pixels).
static void drawText(PopupImage& im, const std::string& t, float x, float y, float px, Rgba c) {
    if (!g_fontOk) return;
    float sc = stbtt_ScaleForMappingEmToPixels(&g_font, px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&g_font, &asc, &desc, &gap);
    float base = y + asc * sc;
    std::vector<uint8_t> bmp;
    for (const char* s = t.c_str(); *s;) {
        int cp = (int)nextCp(s), adv, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&g_font, cp, &adv, &lsb);
        float fx = x - floorf(x);
        stbtt_GetCodepointBitmapBoxSubpixel(&g_font, cp, sc, sc, fx, 0, &x0, &y0, &x1, &y1);
        int w = x1 - x0, h = y1 - y0;
        if (w > 0 && h > 0) {
            bmp.assign((size_t)w * h, 0);
            stbtt_MakeCodepointBitmapSubpixel(&g_font, bmp.data(), w, h, w, sc, sc, fx, 0, cp);
            int ox = (int)floorf(x) + x0, oy = (int)lroundf(base) + y0;
            for (int yy = 0; yy < h; yy++)
                for (int xx = 0; xx < w; xx++) blend(im, ox + xx, oy + yy, c, bmp[yy * w + xx] / 255.f);
        }
        x += adv * sc;
    }
}

// ---------------------------------------------------------------- medal
static bool inStar(float x, float y, float R, float r) {   // centred on 0,0, point up
    float vx[10], vy[10];
    for (int i = 0; i < 10; i++) {
        float a = -1.5707963f + i * 0.6283185f, rr = (i & 1) ? r : R;
        vx[i] = cosf(a) * rr; vy[i] = sinf(a) * rr;
    }
    bool in = false;
    for (int i = 0, j = 9; i < 10; j = i++)
        if (((vy[i] > y) != (vy[j] > y)) && (x < (vx[j] - vx[i]) * (y - vy[i]) / (vy[j] - vy[i]) + vx[i])) in = !in;
    return in;
}

static Rgba hsv(float h, float s, float v) {
    float c = v * s, hp = fmodf(h, 1.f) * 6, x = c * (1 - fabsf(fmodf(hp, 2.f) - 1)), m = v - c;
    float r = 0, g = 0, b = 0;
    if (hp < 1) { r = c; g = x; } else if (hp < 2) { r = x; g = c; } else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; } else if (hp < 5) { r = x; b = c; } else { r = c; b = x; }
    return {r + m, g + m, b + m, 1};
}

static void drawMedal(PopupImage& im, float cx, float cy, float S, int tier) {
    static const uint32_t base[3] = {0xA5602F, 0x8B8780, 0xC9971C}, star[3] = {0xF0B080, 0xE3E0D8, 0xFFF0B0};
    float R = 24 * S, ring = 3 * S, sR = 14.5f * S, sr = 6.2f * S;
    const int SS = 4;
    for (int y = (int)(cy - R) - 1; y <= (int)(cy + R) + 1; y++)
        for (int x = (int)(cx - R) - 1; x <= (int)(cx + R) + 1; x++) {
            float acc[3] = {0, 0, 0}, cov = 0;
            for (int sy = 0; sy < SS; sy++)
                for (int sx = 0; sx < SS; sx++) {
                    float px = x + (sx + .5f) / SS - cx, py = y + (sy + .5f) / SS - cy, d = sqrtf(px * px + py * py);
                    if (d > R) continue;
                    Rgba c;
                    if (d > R - ring) c = hex(0x1A1A1A);
                    else if (inStar(px, py + 0.8f * S, sR, sr)) c = tier == TIER_PLATINUM ? hex(0xFFFFFF) : hex(star[tier]);
                    else {
                        c = tier == TIER_PLATINUM ? hsv(atan2f(py, px) / 6.2831853f + 0.5f, 0.55f, 0.95f) : hex(base[tier]);
                        float shade = 1 - 0.28f * (d / (R - ring)) * (d / (R - ring)) - 0.1f * (py / R);   // light from the top
                        c.r *= shade; c.g *= shade; c.b *= shade;
                    }
                    acc[0] += c.r; acc[1] += c.g; acc[2] += c.b; cov += 1;
                }
            if (cov > 0) blend(im, x, y, {acc[0] / cov, acc[1] / cov, acc[2] / cov, 1}, cov / (SS * SS));
        }
}

// ---------------------------------------------------------------- card
int popupRenderCard(const PopupInfo& info, PopupImage& out) {
    const float S = POPUP_SCALE;
    out.w = (int)lroundf(POPUP_W720 * S);
    out.h = (int)lroundf(POPUP_H720 * S);
    out.px.assign((size_t)out.w * out.h * 4, 0);

    const float barY = 16, barH = 56, iconY = 8, iconSz = 72, titlePx = 17, descPx = 12, maxText = 330;
    std::string title = ellipsize(info.title, titlePx * S, maxText * S) , desc = ellipsize(info.desc, descPx * S, maxText * S);
    float tw = std::max(textWidth(title, titlePx * S), textWidth(desc, descPx * S)) / S;
    tw = std::max(tw, 150.f);

    float barX0 = 0, barX1, iconX, textX, total;
    if (info.iconLeft) {
        iconX = 12; textX = iconX + 90 + 8; barX1 = textX + tw + 26; total = barX1;
    } else {
        textX = 28; barX1 = textX + tw + 18; iconX = barX1 - 4; total = iconX + 90;
    }

    Rgba bar = info.dark ? hex(0x000000, 0.82f) : hex(0xFFFFFF);
    Rgba tc = info.dark ? hex(0xFFFFFF) : hex(0x111111), dc = info.dark ? hex(0xD0D0D0) : hex(0x333333);
    fillRect(out, barX0 * S, barY * S, barX1 * S, (barY + barH) * S, bar);
    drawText(out, title, textX * S, (barY + 9) * S, titlePx * S, tc);
    drawText(out, desc, textX * S, (barY + 33) * S, descPx * S, dc);

    // icon: dark frame, picture inside (or a neutral placeholder)
    float b = 3;
    fillRect(out, iconX * S, iconY * S, (iconX + iconSz) * S, (iconY + iconSz) * S, hex(0x111111));
    int inner = (int)lroundf((iconSz - 2 * b) * S), ix = (int)lroundf((iconX + b) * S), iy = (int)lroundf((iconY + b) * S);
    if (info.icon && info.icon->w > 0) {
        if (info.icon->w == inner && info.icon->h == inner) drawImage(out, *info.icon, ix, iy);
        else { PopupImage sc; popupScaleImage(info.icon->px.data(), info.icon->w, info.icon->h, inner, inner, sc); drawImage(out, sc, ix, iy); }
    } else {
        fillRect(out, (float)ix, (float)iy, (float)(ix + inner), (float)(iy + inner), hex(0x5B6FA8));
    }
    drawMedal(out, (iconX + 42 + 24) * S, (iconY + 38 + 24) * S, S, std::min(std::max(info.tier, 0), 3));
    return (int)ceilf(total * S);
}

int popupTierFor(const char* name, int points) {
    if (name) {
        if (!strcmp(name, "platinum")) return TIER_PLATINUM;
        if (!strcmp(name, "gold")) return TIER_GOLD;
        if (!strcmp(name, "silver")) return TIER_SILVER;
        if (!strcmp(name, "bronze")) return TIER_BRONZE;
    }
    return points >= 50 ? TIER_GOLD : points >= 25 ? TIER_SILVER : TIER_BRONZE;
}

float popupSlide(uint64_t t, uint64_t d) {
    const float in = 260, out = 220;
    if (t >= d) return 0;
    if (t < in) { float k = 1 - t / in; return 1 - k * k * k; }            // ease-out
    if (t > d - out) { float k = 1 - (d - t) / out; return 1 - k * k * k; }   // ease-in on the way out
    return 1;
}
