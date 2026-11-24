// Host test for the popup renderer: renders every variant to PNG.
// g++ -std=gnu++17 -Isysmodule/include -Iinclude sysmodule/test/test_popup.cpp -DPOPUP_DRAW_STB_IMPL source/popup_draw.cpp -o /tmp/test_popup
// /tmp/test_popup <out_dir> [font.ttf] [backdrop_1280x720.png]
#include "popup_draw.h"
#include <cassert>
#include <cstdio>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static std::vector<uint8_t> readAll(const char* p) {
    std::vector<uint8_t> d;
    FILE* f = fopen(p, "rb");
    if (!f) return d;
    fseek(f, 0, SEEK_END); d.resize(ftell(f)); fseek(f, 0, SEEK_SET);
    if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear();
    fclose(f);
    return d;
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "/tmp";
    const char* fontPath = argc > 2 ? argv[2] : "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    std::vector<uint8_t> font = readAll(fontPath);
    if (font.empty()) { printf("no font at %s: skipping render test\n", fontPath); return 0; }
    assert(popupFontInit(font.data()));

    // tiers and slide curve
    assert(popupTierFor(nullptr, 5) == TIER_BRONZE && popupTierFor(nullptr, 25) == TIER_SILVER);
    assert(popupTierFor(nullptr, 100) == TIER_GOLD && popupTierFor("platinum", 5) == TIER_PLATINUM);
    assert(popupSlide(0, 4000) == 0 && popupSlide(2000, 4000) == 1 && popupSlide(4000, 4000) == 0);
    assert(popupSlide(130, 4000) > 0.5f && popupSlide(3900, 4000) < 1);

    // the app icon stands in for a game icon (same 256x256 JPEG format as the system's)
    PopupImage icon;
    int iw, ih, in;
    uint8_t* ipx = stbi_load("icon.jpg", &iw, &ih, &in, 4);
    if (ipx) { icon.w = iw; icon.h = ih; icon.px.assign(ipx, ipx + iw * ih * 4); stbi_image_free(ipx); }

    int bw = 0, bh = 0, bn = 0;
    uint8_t* back = argc > 3 ? stbi_load(argv[3], &bw, &bh, &bn, 4) : nullptr;

    const char* tiers[4] = {"bronze", "silver", "gold", "platinum"};
    for (int dark = 0; dark < 2; dark++)
        for (int left = 0; left < 2; left++)
            for (int t = 0; t < 4; t++) {
                PopupInfo pi;
                pi.title = t == 1 ? "First Day on the Job" : t == 3 ? "Hero of Hyrule — every achievement" : "Rise and Shine";
                pi.desc = t == 1 ? "Complete \"Luigi's Girls\"." : "Find the Old Shirt in the Shrine of Resurrection";
                pi.tier = t; pi.dark = dark; pi.iconLeft = left; pi.icon = icon.w ? &icon : nullptr;
                PopupImage card;
                int w = popupRenderCard(pi, card);
                assert(w > 0 && w <= card.w);
                char path[256];
                snprintf(path, sizeof(path), "%s/popup_%s_%s_%s.png", out, dark ? "dark" : "light", left ? "left" : "right", tiers[t]);
                if (back && bw == 1280 && bh == 720) {   // composite at 720p onto a screenshot, at the real position
                    std::vector<uint8_t> img(back, back + 1280 * 720 * 4);
                    PopupImage small;
                    popupScaleImage(card.px.data(), card.w, card.h, POPUP_W720, POPUP_H720, small);
                    for (int y = 0; y < small.h; y++)
                        for (int x = 0; x < small.w; x++) {
                            uint8_t* s = &small.px[(y * small.w + x) * 4];
                            uint8_t* d = &img[((y + POPUP_Y720) * 1280 + x) * 4];
                            float a = s[3] / 255.f;
                            for (int c = 0; c < 3; c++) d[c] = (uint8_t)(s[c] * a + d[c] * (1 - a));
                        }
                    stbi_write_png(path, 1280, 720, 4, img.data(), 1280 * 4);
                } else {
                    stbi_write_png(path, card.w, card.h, 4, card.px.data(), card.w * 4);
                }
            }

    // ---------------------------------------------------------------- compact toast ("card": "compact")
    // timing: 220 ms in, hold, 180 ms out; 1.7 s hold when others wait
    assert(popupHoldMs(TOAST_HOLD_MS, false) == 3300 && popupHoldMs(TOAST_HOLD_MS, true) == 1700 && popupHoldMs(1000, true) == 1000);
    {
        const uint64_t total = TOAST_IN_MS + TOAST_HOLD_MS + TOAST_OUT_MS;
        assert(popupToastShown(0, total) == 0 && popupToastShown(1000, total) == 1 && popupToastShown(total, total) == 0);
        assert(popupToastShown(110, total) > 0.5f && popupToastShown(total - 90, total) < 0.5f);
        for (uint64_t t = 0; t <= total; t += 10) { float v = popupToastShown(t, total); assert(v >= 0 && v <= 1); }
    }
    // corners: the card ends TOAST_MARGIN px from both screen edges when shown
    for (int c = 0; c < 4; c++) {
        int x, y, lw, lh;
        popupToastLayer(c, TOAST_W, TOAST_H, x, y, lw, lh);
        int cardX = x + popupToastCardX(c, 1.f);
        bool right = c == CORNER_TOP_RIGHT || c == CORNER_BOTTOM_RIGHT, bottom = c >= CORNER_BOTTOM_LEFT;
        assert(lw == TOAST_W + TOAST_SLIDE && lh == TOAST_H);
        assert(right ? cardX + TOAST_W == 1920 - TOAST_MARGIN : cardX == TOAST_MARGIN);
        assert(bottom ? y + TOAST_H == 1080 - TOAST_MARGIN : y == TOAST_MARGIN);
        assert(x >= 0 && x + lw <= 1920);
        int hiddenX = popupToastCardX(c, 0.f);
        assert(hiddenX >= 0 && hiddenX + TOAST_W <= lw);   // the slide stays inside the layer
    }
    int corner = -1;
    assert(popupParseCorner("bottom-right", corner) && corner == CORNER_BOTTOM_RIGHT && !popupParseCorner("middle", corner));
    assert(popupAccentFor(TIER_BRONZE, false) == ACCENT_CYAN && popupAccentFor(TIER_SILVER, false) == ACCENT_CYAN);
    assert(popupAccentFor(TIER_GOLD, false) == ACCENT_GOLD && popupAccentFor(TIER_BRONZE, true) == ACCENT_GOLD);
    assert(popupAccentFor(TIER_PLATINUM, false) == ACCENT_VIOLET);

    struct Sample { const char* file; ToastInfo t; };
    std::vector<Sample> samples;
    auto add = [&](const char* f, const char* label, const char* title, const char* line, uint32_t accent, bool withIcon) {
        Sample s{f, {}};
        s.t.label = label; s.t.title = title; s.t.line = line; s.t.accent = accent; s.t.icon = withIcon && icon.w ? &icon : nullptr;
        samples.push_back(s);
    };
    add("toast_unlock_bronze", "ACHIEVEMENT UNLOCKED", "Escaped Tartarus", "+10 pts", popupAccentFor(TIER_BRONZE, false), true);
    add("toast_unlock_gold", "ACHIEVEMENT UNLOCKED", "Is There No Escape?", "+50 pts", popupAccentFor(TIER_GOLD, false), true);
    add("toast_unlock_platinum", "ACHIEVEMENT UNLOCKED", "Hero of Hyrule", "+100 pts", popupAccentFor(TIER_PLATINUM, false), true);
    add("toast_unlock_hidden", "ACHIEVEMENT UNLOCKED", "Family Secret", "+25 pts", popupAccentFor(TIER_SILVER, true), true);
    add("toast_progress", "PROGRESS", "Pet Cerberus", "5 / 10", ACCENT_CYAN, true);
    add("toast_live_check", "LIVE SAVE", "Working: Hades", "3/41 met \xC2\xB7 read in 80 ms", ACCENT_CYAN, true);
    add("toast_already", "ALREADY UNLOCKED", "Escaped Tartarus", "met in your save", ACCENT_CYAN, true);
    add("toast_long_title", "ACHIEVEMENT UNLOCKED", "Hero of Hyrule \xE2\x80\x94 every shrine, every Korok seed, every memory", "+100 pts",
        ACCENT_GOLD, true);
    add("toast_no_line_no_icon", "ACHIEVEMENT UNLOCKED", "Rise and Shine", "", ACCENT_CYAN, false);
    std::vector<PopupImage> rendered;
    for (const Sample& s : samples) {
        PopupImage img;
        popupRenderToast(s.t, img);
        assert(img.w == TOAST_W && img.h == TOAST_H);
        assert(img.px[3] < 40 && img.px[(TOAST_W / 2 + (TOAST_H / 2) * TOAST_W) * 4 + 3] > 200);   // round corner, opaque middle
        char path[256];
        snprintf(path, sizeof(path), "%s/%s.png", out, s.file);
        stbi_write_png(path, img.w, img.h, 4, img.px.data(), img.w * 4);
        rendered.push_back(img);
    }
    // every sample on a light and a dark backdrop, as on screen (1080p pixels)
    {
        const int pad = 24, cols = 3, rows = (int)(rendered.size() + cols - 1) / cols;
        const int W = cols * (TOAST_W + pad) + pad, H = 2 * (rows * (TOAST_H + pad) + pad);
        std::vector<uint8_t> sheet((size_t)W * H * 4);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint8_t* d = &sheet[((size_t)y * W + x) * 4];
                bool top = y < H / 2;   // top half: bright game scene, bottom half: dark scene
                d[0] = top ? (uint8_t)(150 + 80 * x / W) : (uint8_t)(20 + 30 * x / W);
                d[1] = top ? (uint8_t)(190 - 40 * y / H) : 24;
                d[2] = top ? 120 : (uint8_t)(40 + 30 * y / H);
                d[3] = 255;
            }
        for (int half = 0; half < 2; half++)
            for (size_t i = 0; i < rendered.size(); i++) {
                int ox = pad + (int)(i % cols) * (TOAST_W + pad), oy = half * (H / 2) + pad + (int)(i / cols) * (TOAST_H + pad);
                const PopupImage& img = rendered[i];
                for (int y = 0; y < img.h; y++)
                    for (int x = 0; x < img.w; x++) {
                        const uint8_t* s = &img.px[((size_t)y * img.w + x) * 4];
                        uint8_t* d = &sheet[((size_t)(oy + y) * W + ox + x) * 4];
                        float a = s[3] / 255.f;
                        for (int c = 0; c < 3; c++) d[c] = (uint8_t)(s[c] * a + d[c] * (1 - a));
                    }
            }
        char path[256];
        snprintf(path, sizeof(path), "%s/toast_sheet.png", out);
        stbi_write_png(path, W, H, 4, sheet.data(), W * 4);
    }
    // animation strip: the toast at the top-right corner every 40 ms of its entry and exit, with the fade
    // applied the way the module does it (alpha scaled at blit time)
    {
        const uint64_t total = TOAST_IN_MS + TOAST_HOLD_MS + TOAST_OUT_MS;
        std::vector<uint64_t> ts = {0, 40, 80, 120, 160, 220, total - 180, total - 120, total - 60, total - 20};
        int lw = TOAST_W + TOAST_SLIDE, H = (TOAST_H + 8) * (int)ts.size();
        std::vector<uint8_t> strip((size_t)lw * H * 4, 0);
        for (size_t i = 0; i < ts.size(); i++) {
            float v = popupToastShown(ts[i], total);
            int cx = popupToastCardX(CORNER_TOP_RIGHT, v);
            for (int y = 0; y < TOAST_H; y++)
                for (int x = 0; x < lw; x++) {
                    uint8_t* d = &strip[((size_t)(i * (TOAST_H + 8) + y) * lw + x) * 4];
                    d[0] = 90; d[1] = 120; d[2] = 160; d[3] = 255;
                    int sx = x - cx;
                    if (sx < 0 || sx >= TOAST_W) continue;
                    const uint8_t* s = &rendered[0].px[((size_t)y * TOAST_W + sx) * 4];
                    float a = s[3] / 255.f * v;
                    for (int c = 0; c < 3; c++) d[c] = (uint8_t)(s[c] * a + d[c] * (1 - a));
                }
        }
        char path[256];
        snprintf(path, sizeof(path), "%s/toast_animation.png", out);
        stbi_write_png(path, lw, H, 4, strip.data(), lw * 4);
    }
    printf("popup renders written to %s\n", out);
    return 0;
}
