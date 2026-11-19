// Host test for the popup renderer: renders every variant to PNG.
// g++ -std=gnu++17 -Isysmodule/include -Iinclude sysmodule/test/test_popup.cpp sysmodule/source/popup_draw.cpp -o /tmp/test_popup
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
    printf("popup renders written to %s\n", out);
    return 0;
}
