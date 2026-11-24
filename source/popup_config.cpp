// Popup settings (popup.json), see include/popup_config.h.
#include "popup_config.h"
#include "cJSON.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

struct ThemeColors { const char* name; uint32_t bg, title, line, border; float borderAlpha; uint32_t tile; };
static const ThemeColors THEMES[] = {
    {"dark", 0x181A20, 0xF3F4F6, 0x9CA3AF, 0xFFFFFF, 0.14f, 0x2A2F3A},
    {"light", 0xF5F6F8, 0x111827, 0x4B5563, 0x000000, 0.12f, 0xE3E6EC},
    {"midnight", 0x0F1A2E, 0xE6EEFF, 0x8DA2C8, 0x7AA2FF, 0.28f, 0x1C2B47},
    {"oled", 0x000000, 0xFFFFFF, 0xA1A1AA, 0xFFFFFF, 0.10f, 0x18181B},
    {"warm", 0x2A2118, 0xFFF4E0, 0xC9B79C, 0xFFD9A0, 0.20f, 0x3A2E22},
};

const std::vector<NamedColor>& popupToastThemes() {
    static const std::vector<NamedColor> v = {
        {"dark", "Dark", 0x181A20}, {"light", "Light", 0xF5F6F8}, {"midnight", "Midnight", 0x0F1A2E},
        {"oled", "OLED black", 0x000000}, {"warm", "Warm", 0x2A2118},
    };
    return v;
}

const std::vector<NamedColor>& popupAccents() {
    static const std::vector<NamedColor> v = {
        {"tier", "By tier", ACCENT_CYAN},     {"cyan", "Cyan", ACCENT_CYAN},     {"gold", "Gold", ACCENT_GOLD},
        {"violet", "Violet", ACCENT_VIOLET},  {"green", "Green", 0x4ADE80},      {"pink", "Pink", 0xF472B6},
        {"orange", "Orange", 0xFB923C},       {"red", "Red", 0xF87171},          {"white", "White", 0xF3F4F6},
    };
    return v;
}

const std::vector<NamedSize>& popupToastSizes() {
    static const std::vector<NamedSize> v = {
        {"small", "Small", 1.0f}, {"medium", "Medium", 1.25f}, {"large", "Large", 1.5f}, {"xl", "Extra large", 1.75f},
    };
    return v;
}

const std::vector<int>& popupDurations() {
    static const std::vector<int> v = {0, 2000, 3000, 4000, 5000, 6000, 8000, 10000};
    return v;
}

const char* popupStyleLabel(const std::string& s) {
    if (s == "both") return "On screen + Ultrahand";
    if (s == "ultrahand") return "Ultrahand only";
    if (s == "off") return "Off";
    return "On screen";
}

const char* popupCornerName(int c) {
    static const char* N[] = {"top-left", "top-right", "bottom-left", "bottom-right"};
    return N[std::min(std::max(c, 0), 3)];
}

const char* popupCornerLabel(int c) {
    static const char* N[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
    return N[std::min(std::max(c, 0), 3)];
}

static const ThemeColors& themeOf(const std::string& name) {
    for (const ThemeColors& t : THEMES) if (name == t.name) return t;
    return THEMES[0];
}

static bool knownName(const std::vector<NamedColor>& v, const char* s) {
    for (const NamedColor& c : v) if (!strcmp(c.name, s)) return true;
    return false;
}

bool popupSettingsParse(const char* json, PopupSettings& out, std::string* warnings) {
    cJSON* root = json ? cJSON_Parse(json) : nullptr;
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return false; }
    auto warn = [&](const std::string& w) { if (warnings) *warnings += (warnings->empty() ? "" : "; ") + w; };
    const cJSON* v;
    auto str = [&](const char* k) -> const char* {
        v = cJSON_GetObjectItemCaseSensitive(root, k);
        return cJSON_IsString(v) ? v->valuestring : nullptr;
    };
    auto boolean = [&](const char* k, bool& dst) {
        v = cJSON_GetObjectItemCaseSensitive(root, k);
        if (cJSON_IsBool(v)) dst = cJSON_IsTrue(v);
        else if (v) warn(std::string(k) + " must be true or false");
    };
    const char* s;
    if ((s = str("style"))) {
        if (!strcmp(s, "custom") || !strcmp(s, "both") || !strcmp(s, "ultrahand") || !strcmp(s, "off")) out.style = s;
        else warn(std::string("unknown style '") + s + "'");
    }
    if ((s = str("card"))) out.compact = !strcmp(s, "compact");
    if ((s = str("theme"))) out.classicDark = strcmp(s, "light") != 0;
    if ((s = str("icon"))) out.classicIconLeft = !strcmp(s, "left");
    if ((s = str("position")) && !popupParseCorner(s, out.corner)) warn(std::string("unknown position '") + s + "'");
    if ((s = str("compact_theme"))) {
        if (knownName(popupToastThemes(), s)) out.toastTheme = s;
        else warn(std::string("unknown compact_theme '") + s + "'");
    }
    if ((s = str("size"))) {
        bool known = false;
        for (const NamedSize& z : popupToastSizes()) if (!strcmp(z.name, s)) known = true;
        if (known) out.size = s;
        else warn(std::string("unknown size '") + s + "'");
    }
    if ((s = str("accent"))) {
        if (knownName(popupAccents(), s)) out.accent = s;
        else warn(std::string("unknown accent '") + s + "'");
    }
    v = cJSON_GetObjectItemCaseSensitive(root, "duration_ms");
    if (cJSON_IsNumber(v)) out.durationMs = v->valueint <= 0 ? 0 : std::min(std::max(v->valueint, 1500), 15000);
    v = cJSON_GetObjectItemCaseSensitive(root, "opacity");
    if (cJSON_IsNumber(v)) out.opacity = std::min(std::max(v->valueint, 50), 100);
    boolean("live_check", out.liveCheck);
    boolean("progress", out.progress);
    boolean("border", out.border);
    boolean("show_icon", out.icon);
    boolean("show_label", out.label);
    cJSON_Delete(root);
    return true;
}

std::string popupSettingsJson(const PopupSettings& s) {
    char b[1024];
    snprintf(b, sizeof(b),
             "{\n"
             "  \"style\": \"%s\",\n"
             "  \"card\": \"%s\",\n"
             "  \"duration_ms\": %d,\n"
             "  \"live_check\": %s,\n"
             "  \"progress\": %s,\n"
             "  \"theme\": \"%s\",\n"
             "  \"icon\": \"%s\",\n"
             "  \"position\": \"%s\",\n"
             "  \"compact_theme\": \"%s\",\n"
             "  \"accent\": \"%s\",\n"
             "  \"opacity\": %d,\n"
             "  \"border\": %s,\n"
             "  \"show_icon\": %s,\n"
             "  \"show_label\": %s,\n"
             "  \"size\": \"%s\"\n"
             "}\n",
             s.style.c_str(), s.compact ? "compact" : "classic", s.durationMs, s.liveCheck ? "true" : "false",
             s.progress ? "true" : "false", s.classicDark ? "dark" : "light", s.classicIconLeft ? "left" : "right",
             popupCornerName(s.corner), s.toastTheme.c_str(), s.accent.c_str(), s.opacity, s.border ? "true" : "false",
             s.icon ? "true" : "false", s.label ? "true" : "false", s.size.c_str());
    return b;
}

ToastStyle popupToastStyle(const PopupSettings& s) {
    const ThemeColors& t = themeOf(s.toastTheme);
    ToastStyle st;
    st.bg = t.bg;
    st.bgAlpha = std::min(std::max(s.opacity, 50), 100) / 100.f;
    st.title = t.title;
    st.line = t.line;
    st.border = t.border;
    st.borderAlpha = s.border ? t.borderAlpha : 0.f;
    st.tile = t.tile;
    st.icon = s.icon;
    st.label = s.label;
    for (const NamedSize& z : popupToastSizes()) if (s.size == z.name) st.scale = z.scale;
    return st;
}

static uint32_t darken(uint32_t c, float k) {   // keeps a bright accent readable on a light card
    auto ch = [&](int sh) { return (uint32_t)(((c >> sh) & 255) * k + 0.5f) << sh; };
    return ch(16) | ch(8) | ch(0);
}

ToastInfo popupToastTexts(const PopupText& r, const PopupSettings& s) {
    ToastInfo t;
    t.title = r.title;
    t.accent = popupAccent(s, r.tier, r.hidden);
    const uint32_t plain = popupAccent(s, TIER_BRONZE, false);   // progress / live check: the base accent
    switch (r.kind) {
        case POPUP_PROGRESS: t.label = "PROGRESS"; t.line = r.line; t.accent = plain; break;
        case POPUP_LIVE_CHECK: t.label = "LIVE SAVE"; t.title = "Working: " + r.game; t.line = r.line; t.accent = plain; break;
        case POPUP_KNOWN: t.label = "ALREADY UNLOCKED"; t.line = r.line; break;
        default: t.label = "ACHIEVEMENT UNLOCKED"; t.line = "+" + std::to_string(r.points) + " pts"; break;
    }
    return t;
}

void popupClassicTexts(const PopupText& r, std::string& title, std::string& desc) {
    title = r.title;
    desc = r.desc;
    if (r.kind == POPUP_PROGRESS) desc = "Progress " + r.line + (r.desc.empty() ? "" : " - " + r.desc);
    else if (r.kind == POPUP_LIVE_CHECK) { title = "Live save working - " + r.game; desc = r.line; }
    else if (r.kind == POPUP_KNOWN) desc = "Already unlocked - " + r.line;
}

uint32_t popupAccent(const PopupSettings& s, int tier, bool hidden) {
    uint32_t a = popupAccentFor(tier, hidden);
    if (s.accent != "tier")
        for (const NamedColor& c : popupAccents()) if (s.accent == c.name) a = c.rgb;
    if (s.toastTheme == "light") a = darken(a, 0.62f);
    return a;
}
