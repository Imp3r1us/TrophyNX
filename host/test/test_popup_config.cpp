// Host test of the popup settings (include/popup_config.h): popup.json parsing (old files included), saving,
// themes, accents and the texts of each popup kind. With an output folder and a font, also renders every
// compact theme with every accent to <out>/popup_themes.png.
//   g++ -std=gnu++17 -DPOPUP_DRAW_STB_IMPL -Iinclude host/test/test_popup_config.cpp source/popup_config.cpp source/popup_draw.cpp \
//       -x c source/cJSON.c -o /tmp/test_popup_config
//   /tmp/test_popup_config [out_dir font.ttf]
#include "popup_config.h"
#include <cstdio>
#include <cstring>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static int fails = 0;
static void ok(bool c, const char* m) {
    if (!c) fails++;
    printf("%s %s\n", c ? "ok  " : "FAIL", m);
}

static std::vector<uint8_t> readAll(const char* p) {
    std::vector<uint8_t> d;
    FILE* f = fopen(p, "rb");
    if (!f) return d;
    fseek(f, 0, SEEK_END);
    d.resize(ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear();
    fclose(f);
    return d;
}

int main(int argc, char** argv) {
    // --- defaults: the classic card as before
    PopupSettings d;
    ok(d.style == "custom" && !d.compact && d.durationMs == 0 && d.liveCheck && d.progress && d.classicDark && !d.classicIconLeft &&
           d.corner == CORNER_TOP_LEFT && d.toastTheme == "dark" && d.accent == "tier" && d.opacity == 88 && d.border && d.icon && d.label,
       "defaults");

    // --- an old popup.json (before the settings page) keeps working
    PopupSettings o;
    std::string w;
    ok(popupSettingsParse(R"({ "style": "both", "theme": "light", "icon": "left", "duration_ms": 6000 })", o, &w) && w.empty(),
       "old popup.json parses without warnings");
    ok(o.style == "both" && !o.classicDark && o.classicIconLeft && o.durationMs == 6000 && !o.compact, "old keys kept");

    // --- every key, and bad values reported without breaking the rest
    PopupSettings a;
    w.clear();
    ok(popupSettingsParse(R"({ "card": "compact", "position": "bottom-right", "compact_theme": "midnight", "accent": "pink",
        "opacity": 30, "border": false, "show_icon": false, "show_label": false, "live_check": false, "progress": false,
        "duration_ms": 99999, "style": "loud", "accent2": 1 })", a, &w), "all keys parse");
    ok(a.compact && a.corner == CORNER_BOTTOM_RIGHT && a.toastTheme == "midnight" && a.accent == "pink" && !a.border && !a.icon &&
           !a.label && !a.liveCheck && !a.progress, "all keys read");
    ok(a.opacity == 50 && a.durationMs == 15000, "opacity and duration clamped");
    ok(a.style == "custom" && w.find("unknown style 'loud'") != std::string::npos, "bad style: warning, default kept");
    PopupSettings b;
    w.clear();
    popupSettingsParse(R"({ "compact_theme": "neon", "accent": "rainbow", "position": "center", "border": "yes" })", b, &w);
    ok(b.toastTheme == "dark" && b.accent == "tier" && b.corner == CORNER_TOP_LEFT && b.border, "bad values keep the defaults");
    ok(w.find("neon") != std::string::npos && w.find("rainbow") != std::string::npos && w.find("center") != std::string::npos &&
           w.find("border") != std::string::npos, "each bad value named in the warnings");
    PopupSettings c;
    ok(!popupSettingsParse("not json", c) && !popupSettingsParse("[1,2]", c) && !popupSettingsParse(nullptr, c), "not an object: false");

    // --- save and read back: identical
    std::string js = popupSettingsJson(a);
    PopupSettings r;
    w.clear();
    ok(popupSettingsParse(js.c_str(), r, &w) && w.empty() && popupSettingsJson(r) == js, "saved file reads back identically");
    ok(js.find("\"card\": \"compact\"") != std::string::npos && js.find("\"position\": \"bottom-right\"") != std::string::npos,
       "readable keys");

    // --- style and accents
    ToastStyle st = popupToastStyle(a);
    ok(st.bg == 0x0F1A2E && st.bgAlpha == 0.5f && st.borderAlpha == 0 && !st.icon && !st.label, "midnight theme, 50 %, no border/icon/label");
    ToastStyle def = popupToastStyle(PopupSettings());
    ToastStyle orig;
    ok(def.bg == orig.bg && def.bgAlpha == orig.bgAlpha && def.title == orig.title && def.line == orig.line &&
           def.borderAlpha == orig.borderAlpha, "default settings = the original dark card");
    PopupSettings t;
    ok(popupAccent(t, TIER_BRONZE, false) == ACCENT_CYAN && popupAccent(t, TIER_GOLD, false) == ACCENT_GOLD &&
           popupAccent(t, TIER_SILVER, true) == ACCENT_GOLD && popupAccent(t, TIER_PLATINUM, false) == ACCENT_VIOLET, "accent by tier");
    t.accent = "green";
    ok(popupAccent(t, TIER_PLATINUM, true) == 0x4ADE80, "fixed accent wins over the tier");
    t.toastTheme = "light";
    uint32_t dk = popupAccent(t, TIER_BRONZE, false);
    ok(dk != 0x4ADE80 && ((dk >> 8) & 255) < ((0x4ADE80 >> 8) & 255), "light theme: accent darkened to stay readable");
    for (const NamedColor& th : popupToastThemes()) {
        PopupSettings x;
        x.toastTheme = th.name;
        ok(popupToastStyle(x).bg == th.rgb, (std::string("theme ") + th.name + " has its background").c_str());
    }

    // --- texts of each kind (shared by the module and the app's preview)
    PopupText p;
    p.title = "Pet Cerberus"; p.desc = "Pet Cerberus 10 times"; p.game = "Hades"; p.points = 10; p.line = "5 / 10";
    PopupSettings s;
    p.kind = POPUP_UNLOCK;
    ToastInfo ti = popupToastTexts(p, s);
    ok(ti.label == "ACHIEVEMENT UNLOCKED" && ti.title == "Pet Cerberus" && ti.line == "+10 pts", "unlock texts");
    p.kind = POPUP_PROGRESS;
    ti = popupToastTexts(p, s);
    ok(ti.label == "PROGRESS" && ti.line == "5 / 10" && ti.accent == ACCENT_CYAN, "progress texts");
    p.kind = POPUP_LIVE_CHECK;
    p.line = "3/41 met";
    ti = popupToastTexts(p, s);
    ok(ti.label == "LIVE SAVE" && ti.title == "Working: Hades" && ti.line == "3/41 met", "live check texts");
    std::string title, desc;
    popupClassicTexts(p, title, desc);
    ok(title == "Live save working - Hades" && desc == "3/41 met", "classic card: live check texts");
    p.kind = POPUP_KNOWN;
    p.line = "met in your save";
    popupClassicTexts(p, title, desc);
    ok(title == "Pet Cerberus" && desc == "Already unlocked - met in your save", "classic card: already unlocked texts");

    // --- every theme x a few accents, for the docs
    if (argc > 2) {
        std::vector<uint8_t> font = readAll(argv[2]);
        if (!font.empty() && popupFontInit(font.data())) {
            const char* accents[] = {"tier", "gold", "violet", "green", "pink"};
            const auto& themes = popupToastThemes();
            const int pad = 16, cols = 5, W = cols * (TOAST_W + pad) + pad, H = (int)themes.size() * (TOAST_H + pad) + pad;
            std::vector<uint8_t> sheet((size_t)W * H * 4);
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {   // a game-like backdrop, bright on the left, dark on the right
                    uint8_t* q = &sheet[((size_t)y * W + x) * 4];
                    float k = (float)x / W;
                    q[0] = (uint8_t)(120 * (1 - k) + 18 * k + 40); q[1] = (uint8_t)(170 * (1 - k) + 20 * k + 10);
                    q[2] = (uint8_t)(110 * (1 - k) + 40 * k + 20); q[3] = 255;
                }
            for (size_t ti2 = 0; ti2 < themes.size(); ti2++)
                for (int ai = 0; ai < cols; ai++) {
                    PopupSettings ps;
                    ps.toastTheme = themes[ti2].name;
                    ps.accent = accents[ai];
                    PopupText u;
                    u.title = themes[ti2].label + std::string(" + ") + accents[ai];
                    u.points = 25; u.tier = TIER_GOLD;
                    ToastInfo info = popupToastTexts(u, ps);
                    PopupImage img;
                    popupRenderToast(info, img, popupToastStyle(ps));
                    int ox = pad + ai * (TOAST_W + pad), oy = pad + (int)ti2 * (TOAST_H + pad);
                    for (int y = 0; y < img.h; y++)
                        for (int x = 0; x < img.w; x++) {
                            const uint8_t* s2 = &img.px[((size_t)y * img.w + x) * 4];
                            uint8_t* q = &sheet[((size_t)(oy + y) * W + ox + x) * 4];
                            float al = s2[3] / 255.f;
                            for (int ch = 0; ch < 3; ch++) q[ch] = (uint8_t)(s2[ch] * al + q[ch] * (1 - al));
                        }
                }
            std::string path = std::string(argv[1]) + "/popup_themes.png";
            stbi_write_png(path.c_str(), W, H, 4, sheet.data(), W * 4);
            printf("     %s\n", path.c_str());
        }
    }
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
