#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include "achievements.h"
#include "platform.h"
#include "render.h"
#include "popup_config.h"

#include "version.h"
#include "scan.h"
#include "update.h"
#include "captures.h"
#include "share_server.h"

// ---------------------------------------------------------------- theme
static const uint32_t C_BG = 0xFF12141A, C_PANEL = 0xFF1B1E26, C_PANEL_HI = 0xFF272B36, C_LINE = 0xFF2E3340;
static const uint32_t C_TEXT = 0xFFEDEFF4, C_SUB = 0xFF9BA1AF, C_DIM = 0xFF5B6170;
static const uint32_t C_GOLD = 0xFFF2C14E, C_GREEN = 0xFF4CD08A, C_RED = 0xFFE5676B, C_TRACK = 0xFF2C303B;

static const uint32_t TIER_COLORS[4] = {0xFFCD8B4E, 0xFFC3CAD4, 0xFFF2C14E, 0xFF8FE3F5};
static const char* TIER_NAMES[4] = {"Bronze", "Silver", "Gold", "Platinum"};

// 0 bronze .. 3 platinum: the definition's "tier" if it has one, else from the points (same as the popup medal)
static int tierOf(const Achievement& a) {
    if (a.tier == "bronze") return 0;
    if (a.tier == "silver") return 1;
    if (a.tier == "gold") return 2;
    if (a.tier == "platinum") return 3;
    if (a.points <= 10) return 0;
    if (a.points <= 25) return 1;
    if (a.points <= 50) return 2;
    return 3;
}
static uint32_t tierColor(const Achievement& a) { return TIER_COLORS[tierOf(a)]; }

static uint32_t mixColor(uint32_t a, uint32_t b, float t) {   // t = 0 -> a, 1 -> b (opaque result)
    auto ch = [&](int s) { return (uint32_t)(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t + 0.5f) << s; };
    return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

enum { SCR_LIBRARY = 0, SCR_GAME = 1 };
enum { SORT_FEATURED, SORT_RECENT, SORT_PROGRESS, SORT_PLAYTIME, SORT_NAME, SORT_COUNT };
static const char* SORT_NAMES[SORT_COUNT] = {"Featured", "Recent", "Progress", "Playtime", "A-Z"};
enum { FILT_ALL, FILT_UNLOCKED, FILT_LOCKED, FILT_COUNT };

struct UI {
    Canvas cv;
    Font font;
    Library lib;
    std::vector<Image> icons;
    std::vector<bool> iconTried;
    bool classic = false;     // the original layout (game list + details side by side), saved in theme.txt
    int iconSize = 68;        // grid icons (new look) or list icons (classic look)
    int listScroll = 0;       // classic look: first visible game in the list
    int screen = SCR_LIBRARY; // classic look: SCR_GAME = focus on the achievement list
    int sel = 0;              // index in lib.games of the selected game
    int sortMode = SORT_FEATURED;
    std::vector<int> order;   // lib.games indices in grid order (Overview first)
    int gridPos = 0, gridScroll = 0;   // position in `order`, first visible row
    int achFilter = FILT_ALL;
    std::vector<int> achView; // achievement indices of the selected game shown with the current filter
    int achSel = 0, achScroll = 0;     // position in achView
    int relockAch = -1;       // achievement index for confirm == 1
    int heroGi = -1;          // game whose large icon is in heroIcon
    Image heroIcon;
    uint32_t heroTint = 0;
    bool popup = false;
    bool debug = false;
    int confirm = 0;          // 0 none, 1 one achievement, 2 whole game, 3 everything
    std::string toast;        // short message shown at the bottom after an action
    int debugScroll = 0;
    int lastProgress = -1;
    const char* loadingLabel = "Reading your games";
    int upd = 0;              // update panel: 0 none, 1 check result, 2 install result
    UpdateInfo updInfo;
    bool updOk = false;
    std::string updResult;
    int cap = 0;              // save captures panel: 0 none, 1 panel, 2 sharing over Wi-Fi
    int capDelete = 0;        // - pressed once: press again to delete
    CaptureSettings capSet;
    std::vector<CaptureInfo> capList;
    ShareServer server;
    std::string shareIp, shareErr;
    bool pset = false;        // popup settings panel (right stick)
    int psSel = 0, psKind = 0;   // selected row, preview kind (unlock / progress / live check / already unlocked)
    PopupSettings ps;
    bool psChanged = false;
    std::string psWarn, psKey;   // psKey: what the cached preview images show
    Image psBig, psMini;
    int psMiniX = 0, psMiniY = 0;
};

static const int SHARE_PORT = 8080;

static UI* g_ui = nullptr;

static std::string fmtInt(long long v) {
    std::string s = std::to_string(v), out;
    int n = 0;
    for (int i = (int)s.size() - 1; i >= 0; i--) {
        out.insert(out.begin(), s[i]);
        if (++n % 3 == 0 && i > 0 && s[i - 1] != '-') out.insert(out.begin(), ',');
    }
    return out;
}

static std::string fmtNum(double v) {
    if (fabs(v - llround(v)) < 0.05) return fmtInt(llround(v));
    char b[32];
    snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

static std::string fmtDate(uint64_t t) {
    if (!t) return "";
    time_t tt = (time_t)t;
    struct tm* tmv = gmtime(&tt);
    if (!tmv) return "";
    char b[32];
    strftime(b, sizeof(b), "%d %b %Y", tmv);
    return b;
}

// ---------------------------------------------------------------- drawing helpers
static void drawTrophy(Canvas& c, float cx, float cy, float s, uint32_t col) {
    // cup
    c.fillRound(cx - s * 0.42f, cy - s * 0.5f, s * 0.84f, s * 0.55f, s * 0.22f, col);
    c.fillRect((int)(cx - s * 0.42f), (int)(cy - s * 0.5f), (int)(s * 0.84f), (int)(s * 0.2f), col);
    // handles
    c.ring(cx - s * 0.42f, cy - s * 0.3f, s * 0.2f, s * 0.08f, col);
    c.ring(cx + s * 0.42f, cy - s * 0.3f, s * 0.2f, s * 0.08f, col);
    // stem + base
    c.fillRect((int)(cx - s * 0.07f), (int)(cy + s * 0.02f), (int)(s * 0.14f), (int)(s * 0.22f), col);
    c.fillRound(cx - s * 0.3f, cy + s * 0.22f, s * 0.6f, s * 0.16f, s * 0.05f, col);
}

static void drawLock(Canvas& c, float cx, float cy, float s, uint32_t col) {
    c.ring(cx, cy - s * 0.12f, s * 0.22f, s * 0.08f, col);
    c.fillRound(cx - s * 0.3f, cy - s * 0.08f, s * 0.6f, s * 0.46f, s * 0.08f, col);
}

static void drawCheck(Canvas& c, float cx, float cy, float s, uint32_t col) {
    c.line(cx - s * 0.28f, cy + s * 0.02f, cx - s * 0.08f, cy + s * 0.22f, s * 0.12f, col);
    c.line(cx - s * 0.08f, cy + s * 0.22f, cx + s * 0.3f, cy - s * 0.2f, s * 0.12f, col);
}

static void progressBar(Canvas& c, int x, int y, int w, int h, double frac, uint32_t col) {
    frac = std::max(0.0, std::min(1.0, frac));
    c.fillRound((float)x, (float)y, (float)w, (float)h, h / 2.0f, C_TRACK);
    if (frac > 0) c.fillRound((float)x, (float)y, std::max((float)h, (float)(w * frac)), (float)h, h / 2.0f, col);
}

static void buttonHint(UI& ui, int& x, int y, const char* btn, const char* label) {
    Canvas& c = ui.cv;
    int tw = ui.font.measure(15, btn);
    int bw = std::max(26, tw + 14);   // round for one letter, a pill for "ZR", "L/R"...
    c.fillRound((float)x, (float)y, (float)bw, 26.f, 13.f, C_TEXT);
    ui.font.draw(c, x + bw / 2 - tw / 2, y + 4, 15, btn, C_BG);
    x += bw + 6;
    x += ui.font.draw(c, x, y + 2, 19, label, C_SUB) + 26;
}

// Clockwise arc from 12 o'clock covering `frac` of a ring, with round ends.
static void progressRing(Canvas& c, float cx, float cy, float r, float thick, double frac, uint32_t col) {
    c.ring(cx, cy, r, thick, C_TRACK);
    frac = std::max(0.0, std::min(1.0, frac));
    if (frac <= 0) return;
    const float PI2 = 6.2831853f;
    float end = (float)frac * PI2;
    int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1), y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
        float cov = std::min(r - d + 0.5f, d - (r - thick) + 0.5f);
        if (cov <= 0) continue;
        float a = atan2f(dx, -dy);
        if (a < 0) a += PI2;
        if (a <= end) c.blend(x, y, col, cov);
    }
    float mr = r - thick / 2;
    c.fillCircle(cx, cy - mr, thick / 2, col);
    c.fillCircle(cx + mr * sinf(end), cy - mr * cosf(end), thick / 2, col);
}

static std::string fmtHours(uint64_t ns) {
    double h = ns / 3.6e12;
    if (h < 1) return std::to_string((int)(ns / 6e10)) + " min";
    if (h < 10) return fmtNum(floor(h * 10) / 10) + " h";
    return fmtInt((long long)h) + " h";
}

static const int ICON = 68;   // game icon size in the library grid

static const Image* gameIcon(UI& ui, int gi) {
    if (gi <= 0) return nullptr;
    if ((int)ui.icons.size() != (int)ui.lib.games.size()) {
        ui.icons.assign(ui.lib.games.size(), Image());
        ui.iconTried.assign(ui.lib.games.size(), false);
    }
    if (!ui.iconTried[gi]) {
        ui.iconTried[gi] = true;
        decodeIcon(ui.lib.games[gi].iconJpeg, ui.iconSize, ui.icons[gi]);
    }
    return ui.icons[gi].valid() ? &ui.icons[gi] : nullptr;
}

// Large icon of the game page (one at a time) and its average colour, used to tint the page header.
static const Image* heroIcon(UI& ui, int gi) {
    if (gi <= 0) return nullptr;
    if (ui.heroGi != gi) {
        ui.heroGi = gi;
        ui.heroIcon = Image();
        ui.heroTint = 0;
        if (decodeIcon(ui.lib.games[gi].iconJpeg, 136, ui.heroIcon)) {
            uint64_t r = 0, g = 0, b = 0;
            for (uint32_t p : ui.heroIcon.px) { r += p & 255; g += (p >> 8) & 255; b += (p >> 16) & 255; }
            uint64_t n = ui.heroIcon.px.size();
            ui.heroTint = 0xFF000000u | (uint32_t)(r / n) << 16 | (uint32_t)(g / n) << 8 | (uint32_t)(b / n);
        }
    }
    return ui.heroIcon.valid() ? &ui.heroIcon : nullptr;
}

static void drawIconTile(UI& ui, int gi, int x, int y, int size, const Image* img) {
    Canvas& c = ui.cv;
    const Game& g = ui.lib.games[gi];
    float rad = size * 0.16f;
    if (gi == 0) {
        c.fillRound(x, y, size, size, rad, 0xFF3A3220);
        drawTrophy(c, x + size / 2.f, y + size * 0.54f, size * 0.58f, C_GOLD);
    } else if (img) {
        c.blitRounded(*img, x, y, rad, g.stats.launches == 0);
    } else {
        c.fillRound(x, y, size, size, rad, 0xFF343A4A);
        size_t i = 0;
        if (!g.name.empty()) decodeUtf8(g.name, i);
        std::string ch = g.name.substr(0, i);
        int fs = size * 4 / 10;
        ui.font.draw(c, x + size / 2 - ui.font.measure(fs, ch) / 2, y + size / 2 - fs * 6 / 10, fs, ch, C_SUB);
    }
}

struct TierCount { int got[4] = {0, 0, 0, 0}, all[4] = {0, 0, 0, 0}; };
static void addTiers(TierCount& t, const Game& g) {
    for (const Achievement& a : g.achs) {
        int k = tierOf(a);
        t.all[k]++;
        if (a.unlocked) t.got[k]++;
    }
}

static bool isComplete(const Game& g) { return !g.achs.empty() && g.unlocked == (int)g.achs.size(); }

// ---------------------------------------------------------------- lists
// Grid order for the current sort tab (Overview always first); keeps the selected game selected.
static void buildOrder(UI& ui) {
    const Library& lib = ui.lib;
    ui.order.clear();
    for (int i = 1; i < (int)lib.games.size(); i++) ui.order.push_back(i);
    auto frac = [&](int i) { const Game& g = lib.games[i]; return g.achs.empty() ? 0.0 : (double)g.unlocked / g.achs.size(); };
    auto lower = [](std::string s) { for (char& ch : s) if (ch >= 'A' && ch <= 'Z') ch += 32; return s; };
    std::vector<int>& o = ui.order;
    switch (ui.sortMode) {
        case SORT_RECENT:
            std::stable_sort(o.begin(), o.end(), [&](int a, int b) { return lib.games[a].stats.lastPlayed > lib.games[b].stats.lastPlayed; });
            break;
        case SORT_PROGRESS:
            std::stable_sort(o.begin(), o.end(), [&](int a, int b) {
                double fa = frac(a), fb = frac(b);
                if (fa != fb) return fa > fb;
                return lib.games[a].unlocked > lib.games[b].unlocked;
            });
            break;
        case SORT_PLAYTIME:
            std::stable_sort(o.begin(), o.end(), [&](int a, int b) { return lib.games[a].stats.playtimeNs > lib.games[b].stats.playtimeNs; });
            break;
        case SORT_NAME:
            std::stable_sort(o.begin(), o.end(), [&](int a, int b) { return lower(lib.games[a].name) < lower(lib.games[b].name); });
            break;
        default: break;   // featured: the library's own order (games with a set first, then recently played)
    }
    o.insert(o.begin(), 0);
    ui.gridPos = 0;
    for (size_t i = 0; i < o.size(); i++) if (o[i] == ui.sel) ui.gridPos = (int)i;
}

// Achievements of the selected game shown with the current filter (unlocked: most recent first).
static void buildAchView(UI& ui) {
    const Game& g = ui.lib.games[ui.sel];
    ui.achView.clear();
    for (int i = 0; i < (int)g.achs.size(); i++) {
        if (ui.achFilter == FILT_ALL || (ui.achFilter == FILT_UNLOCKED) == g.achs[i].unlocked) ui.achView.push_back(i);
    }
    if (ui.achFilter == FILT_UNLOCKED)
        std::stable_sort(ui.achView.begin(), ui.achView.end(), [&](int a, int b) { return g.achs[a].unlockTime > g.achs[b].unlockTime; });
}

// ---------------------------------------------------------------- look (theme)
static std::string themePath() { return plat::dataDir() + "/theme.txt"; }

static void setLook(UI& ui, bool classic) {
    ui.classic = classic;
    ui.iconSize = classic ? 56 : 68;
    ui.icons.clear();   // decoded again at the new size
    ui.achFilter = FILT_ALL;   // the classic look has no filters
    ui.achSel = ui.achScroll = 0;
    buildAchView(ui);
    buildOrder(ui);     // new look: put the grid cursor on the selected game
}

static void loadLook(UI& ui) {
    std::vector<uint8_t> d;
    bool classic = readFile(themePath(), d) && std::string(d.begin(), d.end()).find("classic") == 0;
    setLook(ui, classic);
}

static bool saveLook(UI& ui) {
    makeDirs(plat::dataDir());
    return writeFile(themePath(), ui.classic ? "classic" : "new");
}

// Header chip that says how to switch look (press either stick)
static int chip(UI& ui, int x, int y, const char* btn, const char* label) {
    Canvas& c = ui.cv;
    int bw = ui.font.measure(13, btn) + 12, lw = ui.font.measure(15, label);
    int w = 6 + bw + 8 + lw + 12;
    c.fillRound(x, y, w, 28, 14, C_PANEL_HI);
    c.fillRound(x + 4, y + 4, bw, 20, 10, C_SUB);
    ui.font.draw(c, x + 4 + 6, y + 6, 13, btn, C_BG);
    ui.font.draw(c, x + 6 + bw + 8, y + 5, 15, label, C_TEXT);
    return w;
}

static void themeChip(UI& ui, int x, int y) {
    int w = chip(ui, x, y, "LS", ui.classic ? "New look" : "Classic look");
    chip(ui, x + w + 10, y, "RS", "Popup settings");
}

// ---------------------------------------------------------------- screens
static void drawHeader(UI& ui) {
    Canvas& c = ui.cv;
    c.gradientV(0, 0, 1280, 76, 0xFF1F2330, C_BG);
    c.fillCircle(48, 38, 24, 0xFF2A2F3D);
    drawTrophy(c, 48, 40, 28, C_GOLD);
    int tw = ui.font.draw(c, 84, 18, 28, "TrophyNX", C_TEXT);
    themeChip(ui, 84 + tw + 24, 24);
    std::string user = ui.font.ellipsize(20, plat::userName(), 300);
    int uw = ui.font.measure(20, user);
    ui.font.draw(c, 1256 - uw, 24, 20, user, C_TEXT);
    // avatar: first letter of the user name
    float ax = 1256 - uw - 28;
    c.fillCircle(ax, 38, 18, C_PANEL_HI);
    size_t i = 0;
    std::string ini = user.empty() ? "?" : (decodeUtf8(user, i), user.substr(0, i));
    ui.font.draw(c, (int)ax - ui.font.measure(18, ini) / 2, 26, 18, ini, C_GOLD);
    c.fillRect(0, 76, 1280, 1, C_LINE);
}

// L [tab][tab][tab] R
static void drawTabs(UI& ui, int x, int y, const std::vector<std::string>& tabs, int sel) {
    Canvas& c = ui.cv;
    auto shoulder = [&](const char* t) {
        int tw = ui.font.measure(14, t);
        c.fillRound(x, y + 6, 30, 22, 7, C_LINE);
        ui.font.draw(c, x + 15 - tw / 2, y + 8, 14, t, C_SUB);
        x += 40;
    };
    shoulder("L");
    for (size_t i = 0; i < tabs.size(); i++) {
        int tw = ui.font.measure(17, tabs[i]), w = tw + 32;
        bool on = (int)i == sel;
        if (on) c.fillRound(x, y, w, 34, 17, C_GOLD);
        ui.font.draw(c, x + 16, y + 6, 17, tabs[i], on ? C_BG : C_SUB);
        x += w + 4;
    }
    x += 6;
    shoulder("R");
}

static const int GRID_X = 24, GRID_Y = 256, COLS = 3, CARD_W = 402, CARD_H = 92, GAP_X = 13, GAP_Y = 10, GRID_ROWS = 4;

static void drawProfile(UI& ui) {
    Canvas& c = ui.cv;
    Library& lib = ui.lib;
    int x = 24, y = 88, w = 1232, h = 108;
    c.fillRound(x, y, w, h, 14, C_PANEL);

    // points and overall completion
    int pw = ui.font.draw(c, x + 24, y + 10, 40, fmtInt(lib.totalPoints), C_GOLD);
    ui.font.draw(c, x + 24 + pw + 8, y + 30, 18, "points", C_SUB);
    double frac = lib.totalCount ? (double)lib.totalUnlocked / lib.totalCount : 0;
    ui.font.draw(c, x + 24, y + 62, 16, fmtInt(lib.totalUnlocked) + " / " + fmtInt(lib.totalCount) + " achievements", C_SUB);
    std::string pct = std::to_string((int)floor(frac * 100)) + "%";
    ui.font.draw(c, x + 354 - ui.font.measure(16, pct), y + 62, 16, pct, C_TEXT);
    progressBar(c, x + 24, y + 88, 330, 6, frac, C_GOLD);

    // medals by tier
    c.fillRect(x + 380, y + 20, 1, h - 40, C_LINE);
    TierCount t;
    for (const Game& g : lib.games) addTiers(t, g);
    for (int k = 0; k < 4; k++) {
        float mx = x + 424 + k * 104, my = y + h / 2.f;
        c.fillCircle(mx, my, 22, TIER_COLORS[k]);
        c.fillCircle(mx, my, 18, C_PANEL);
        drawTrophy(c, mx, my + 2, 22, TIER_COLORS[k]);
        ui.font.draw(c, (int)mx + 30, y + 28, 24, fmtInt(t.got[k]), C_TEXT);
        ui.font.draw(c, (int)mx + 30, y + 60, 14, TIER_NAMES[k], C_SUB);
    }

    // latest unlocks
    int lx = x + 846;
    c.fillRect(lx - 20, y + 20, 1, h - 40, C_LINE);
    ui.font.draw(c, lx, y + 12, 14, "LATEST UNLOCKS", C_DIM);
    std::vector<std::pair<uint64_t, std::pair<int, int>>> rec;
    for (int gi = 0; gi < (int)lib.games.size(); gi++)
        for (int ai = 0; ai < (int)lib.games[gi].achs.size(); ai++) {
            const Achievement& a = lib.games[gi].achs[ai];
            if (a.unlocked && a.unlockTime) rec.push_back({a.unlockTime, {gi, ai}});
        }
    size_t shown = std::min<size_t>(2, rec.size());
    std::partial_sort(rec.begin(), rec.begin() + shown, rec.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (!shown) ui.font.draw(c, lx, y + 46, 17, "Nothing yet - keep playing!", C_SUB);
    for (size_t i = 0; i < shown; i++) {
        const Game& g = lib.games[rec[i].second.first];
        const Achievement& a = g.achs[rec[i].second.second];
        int ey = y + 38 + (int)i * 32;
        uint32_t tc = tierColor(a);
        c.fillCircle(lx + 11, ey + 13, 11, tc);
        c.fillCircle(lx + 11, ey + 13, 8.5f, C_PANEL);
        drawTrophy(c, lx + 11, ey + 14, 11, tc);
        int nw = ui.font.draw(c, lx + 30, ey + 2, 17, a.name, C_TEXT, 200);
        ui.font.draw(c, lx + 30 + nw + 10, ey + 5, 14, g.name, C_DIM, x + w - 20 - (lx + 30 + nw + 10));
    }
}

static void drawLibrary(UI& ui) {
    Canvas& c = ui.cv;
    Library& lib = ui.lib;
    drawProfile(ui);
    drawTabs(ui, GRID_X, 208, std::vector<std::string>(SORT_NAMES, SORT_NAMES + SORT_COUNT), ui.sortMode);
    int played = 0, completed = 0;
    uint64_t ns = 0;
    for (size_t i = 1; i < lib.games.size(); i++) {
        if (lib.games[i].stats.launches > 0) played++;
        if (isComplete(lib.games[i])) completed++;
        ns += lib.games[i].stats.playtimeNs;
    }
    std::string st = std::to_string(lib.games.size() - 1) + " games  \xC2\xB7  " + std::to_string(played) + " played  \xC2\xB7  " +
                     fmtHours(ns) + "  \xC2\xB7  " + std::to_string(completed) + " completed";
    ui.font.draw(c, 1256 - ui.font.measure(16, st), 216, 16, st, C_SUB);

    int n = (int)ui.order.size();
    for (int r = 0; r < GRID_ROWS; r++) for (int col = 0; col < COLS; col++) {
        int p = (ui.gridScroll + r) * COLS + col;
        if (p >= n) break;
        int gi = ui.order[p];
        const Game& g = lib.games[gi];
        int x = GRID_X + col * (CARD_W + GAP_X), y = GRID_Y + r * (CARD_H + GAP_Y);
        bool selected = p == ui.gridPos;
        if (selected) {
            c.fillRound(x - 3, y - 3, CARD_W + 6, CARD_H + 6, 15, C_GOLD);
            c.fillRound(x, y, CARD_W, CARD_H, 12, C_PANEL_HI);
        } else c.fillRound(x, y, CARD_W, CARD_H, 12, C_PANEL);
        int ix = x + 12, iy = y + 12;
        drawIconTile(ui, gi, ix, iy, ICON, gameIcon(ui, gi));
        bool complete = isComplete(g);
        if (complete) {
            float bx = ix + ICON - 3, by = iy + ICON - 3;
            c.fillCircle(bx, by, 12, selected ? C_PANEL_HI : C_PANEL);
            c.fillCircle(bx, by, 9.5f, C_GREEN);
            drawCheck(c, bx, by, 15, C_BG);
        }
        int tx = ix + ICON + 14, tw = x + CARD_W - 16 - tx;
        bool played = gi == 0 || g.stats.launches > 0;
        ui.font.draw(c, tx, y + 11, 19, g.name, played ? C_TEXT : C_SUB, tw - (g.hasDefinition ? 26 : 0));
        if (g.hasDefinition) {   // has its own achievement set (not only play time)
            float bx = x + CARD_W - 24, by = y + 24;
            c.fillCircle(bx, by, 11, 0xFF3A3220);
            drawTrophy(c, bx, by + 1, 13, C_GOLD);
        }
        std::string sub;
        if (gi == 0) sub = "Your whole library";
        else if (!g.stats.valid || g.stats.launches == 0) sub = "Not played yet";
        else sub = fmtHours(g.stats.playtimeNs) + " played" + (g.stats.lastPlayed ? "  \xC2\xB7  " + fmtDate(g.stats.lastPlayed) : "");
        ui.font.draw(c, tx, y + 38, 15, sub, C_SUB, tw);
        double frac = g.achs.empty() ? 0 : (double)g.unlocked / g.achs.size();
        std::string cnt = std::to_string(g.unlocked) + " / " + std::to_string(g.achs.size());
        int cw = ui.font.measure(15, cnt);
        progressBar(c, tx, y + 70, tw - cw - 12, 6, frac, complete ? C_GREEN : C_GOLD);
        ui.font.draw(c, tx + tw - cw, y + 62, 15, cnt, complete ? C_GREEN : C_SUB);
    }
    int rows = (n + COLS - 1) / COLS;
    if (rows > GRID_ROWS) {
        int track = GRID_ROWS * (CARD_H + GAP_Y) - GAP_Y;
        int th = std::max(30, track * GRID_ROWS / rows);
        int ty = GRID_Y + (track - th) * ui.gridScroll / std::max(1, rows - GRID_ROWS);
        c.fillRound(1266, GRID_Y, 4, track, 2, C_TRACK);
        c.fillRound(1266, ty, 4, th, 2, C_DIM);
    }
}

static const int ACH_X = 24, ACH_W = 1232, ACH_Y = 320, ACH_H = 66, ACH_ROWS = 5;

static void drawGamePage(UI& ui) {
    Canvas& c = ui.cv;
    Library& lib = ui.lib;
    const Game& g = lib.games[ui.sel];
    bool complete = isComplete(g);
    double frac = g.achs.empty() ? 0 : (double)g.unlocked / g.achs.size();

    // header card: icon, name, play stats, save status, medals, completion ring
    int x = 24, y = 88, w = 1232, h = 172;
    const Image* img = heroIcon(ui, ui.sel);
    c.fillRound(x, y, w, h, 16, img ? mixColor(C_PANEL, ui.heroTint, 0.22f) : C_PANEL);
    drawIconTile(ui, ui.sel, x + 18, y + 18, 136, img);
    int tx = x + 18 + 136 + 24, textW = x + w - 190 - tx;
    ui.font.draw(c, tx, y + 14, 30, g.name, C_TEXT, textW);

    std::string meta;
    const std::string dot = "  \xC2\xB7  ";
    if (ui.sel == 0) {
        int played = 0, completed = 0;
        uint64_t ns = 0;
        for (size_t i = 1; i < lib.games.size(); i++) {
            if (lib.games[i].stats.launches > 0) played++;
            if (isComplete(lib.games[i])) completed++;
            ns += lib.games[i].stats.playtimeNs;
        }
        meta = std::to_string(played) + " games played" + dot + fmtHours(ns) + dot + std::to_string(completed) + " completed";
    } else if (!g.stats.valid || g.stats.launches == 0) {
        meta = "Not played yet";
    } else {
        meta = fmtHours(g.stats.playtimeNs) + " played" + dot + std::to_string(g.stats.launches) + (g.stats.launches == 1 ? " launch" : " launches");
        if (g.stats.lastPlayed) meta += dot + "last played " + fmtDate(g.stats.lastPlayed);
    }
    ui.font.draw(c, tx, y + 56, 17, meta, C_SUB, textW);

    std::string status;
    uint32_t scol = C_SUB;
    if (ui.sel == 0) {
        if (!lib.loadErrors.empty()) { status = "Some definition files have errors (press X)"; scol = C_RED; }
        else if (lib.liveStatus.find("running") == 0 || lib.liveStatus.find("started now") != std::string::npos) { status = "Live module: " + lib.liveStatus; scol = C_GREEN; }
        else { status = "Live module: " + lib.liveStatus; scol = lib.liveStatus.find("not installed") == 0 ? C_SUB : C_RED; }
    } else if (g.hasDefinition && g.reader.empty()) {
        status = "Themed play-time set (save format not supported yet)";
    } else if (g.hasDefinition) {
        switch (g.saveStatus) {
            case SaveStatus::Ok: status = "Save data read"; scol = C_GREEN; break;
            case SaveStatus::NoSave: status = "No save data for this user yet (if the game is suspended, close it and press Y)"; scol = C_GOLD; break;
            case SaveStatus::Unreadable: status = "Save found but not readable (unsupported version?)"; scol = C_RED; break;
            case SaveStatus::NeedsKey: status = "Encrypted save: needs its password once (X for how, or imp3r1us.github.io/TrophyNX/#key)"; scol = C_GOLD; break;
            default: break;
        }
    } else {
        status = "Play-time achievements only (no game-specific set yet)";
    }
    ui.font.draw(c, tx, y + 84, 16, status, scol, textW);

    TierCount t;
    addTiers(t, g);
    int px = tx;
    for (int k = 0; k < 4; k++) {
        if (!t.all[k]) continue;
        float mx = px + 13, my = y + 136;
        c.fillCircle(mx, my, 13, TIER_COLORS[k]);
        c.fillCircle(mx, my, 10, img ? mixColor(C_PANEL, ui.heroTint, 0.22f) : C_PANEL);
        drawTrophy(c, mx, my + 1, 13, TIER_COLORS[k]);
        std::string s = std::to_string(t.got[k]) + "/" + std::to_string(t.all[k]);
        px += 32 + ui.font.draw(c, px + 32, y + 125, 17, s, t.got[k] == t.all[k] ? TIER_COLORS[k] : C_TEXT) + 22;
    }
    ui.font.draw(c, px + 6, y + 126, 16, fmtInt(g.points) + " / " + fmtInt(g.maxPoints) + " pts", C_GOLD);

    float rcx = x + w - 96, rcy = y + h / 2.f;
    uint32_t rc = complete ? C_GREEN : C_GOLD;
    progressRing(c, rcx, rcy, 64, 10, frac, rc);
    std::string pct = std::to_string((int)floor(frac * 100)) + "%";
    ui.font.draw(c, (int)rcx - ui.font.measure(30, pct) / 2, (int)rcy - 26, 30, pct, rc);
    std::string cnt = std::to_string(g.unlocked) + " / " + std::to_string(g.achs.size());
    ui.font.draw(c, (int)rcx - ui.font.measure(15, cnt) / 2, (int)rcy + 12, 15, cnt, C_SUB);

    // filter tabs
    int nu = g.unlocked, na = (int)g.achs.size();
    std::vector<std::string> tabs = {"All  " + std::to_string(na), "Unlocked  " + std::to_string(nu), "Locked  " + std::to_string(na - nu)};
    drawTabs(ui, ACH_X, 272, tabs, ui.achFilter);
    if (ui.achFilter == FILT_UNLOCKED && nu) ui.font.draw(c, 1256 - ui.font.measure(15, "most recent first"), 281, 15, "most recent first", C_DIM);

    // achievement list
    c.fillRound(ACH_X, ACH_Y - 6, ACH_W, ACH_H * ACH_ROWS + 12, 14, C_PANEL);
    if (ui.achView.empty()) {
        const char* e = na == 0 ? "No achievements here." : ui.achFilter == FILT_UNLOCKED ? "Nothing unlocked yet - keep playing!" : "Everything is unlocked. Well done!";
        ui.font.draw(c, 640 - ui.font.measure(19, e) / 2, ACH_Y + ACH_H * ACH_ROWS / 2 - 12, 19, e, C_SUB);
        return;
    }
    c.setClip(ACH_X, ACH_Y - 6, ACH_W, ACH_H * ACH_ROWS + 12);
    for (int r = 0; r < ACH_ROWS; r++) {
        int vi = ui.achScroll + r;
        if (vi >= (int)ui.achView.size()) break;
        const Achievement& a = g.achs[ui.achView[vi]];
        int ry = ACH_Y + r * ACH_H;
        bool selr = vi == ui.achSel;
        uint32_t rowBg = selr ? C_PANEL_HI : C_PANEL;
        if (selr) {
            c.fillRound(ACH_X + 8, ry + 1, ACH_W - 26, ACH_H - 2, 10, C_PANEL_HI);
            c.fillRound(ACH_X + 8, ry + 15, 4, ACH_H - 30, 2, C_GOLD);
        } else if (r > 0) c.fillRect(ACH_X + 92, ry, ACH_W - 116, 1, C_LINE);
        float bx = ACH_X + 50, by = ry + ACH_H / 2.f;
        uint32_t tc = tierColor(a);
        if (a.unlocked) {
            c.fillCircle(bx, by, 23, tc);
            c.fillCircle(bx, by, 19, rowBg);
            drawTrophy(c, bx, by + 2, 22, tc);
        } else {
            c.fillCircle(bx, by, 23, 0xFF2C303B);
            c.ring(bx, by, 23, 2, mixColor(rowBg, tc, 0.45f));   // which medal it will be
            drawLock(c, bx, by + 2, 22, C_DIM);
        }

        // right side: points pill, then date / progress
        int rx = ACH_X + ACH_W - 24;
        std::string pts = std::to_string(a.points);
        int ptw = std::max(54, ui.font.measure(15, pts) + 22);   // fixed width keeps the dates/progress aligned
        c.fillRound(rx - ptw, by - 13, ptw, 26, 13, a.unlocked ? mixColor(rowBg, tc, 0.22f) : C_TRACK);
        ui.font.draw(c, rx - ptw / 2 - ui.font.measure(15, pts) / 2, (int)by - 11, 15, pts, a.unlocked ? tc : C_DIM);
        int rx2 = rx - ptw - 20;
        int rightW = 230;
        if (a.unlocked) {
            std::string d = fmtDate(a.unlockTime);
            int dw = ui.font.measure(16, d);
            c.fillCircle(rx2 - dw - 14, by, 9, C_GREEN);
            drawCheck(c, rx2 - dw - 14, by, 15, C_BG);
            ui.font.draw(c, rx2 - dw, (int)by - 11, 16, d, C_GREEN);
        } else if (a.rule.showProgress && !a.hidden && a.rule.target > 0) {
            const Rule& rl = a.rule;
            double cur = std::min(a.value, rl.target) / rl.divisor, tgt = rl.target / rl.divisor;
            std::string p = fmtNum(floor(cur * 10) / 10) + " / " + fmtNum(tgt);
            ui.font.draw(c, rx2 - ui.font.measure(15, p), ry + 12, 15, p, C_TEXT);
            progressBar(c, rx2 - 170, ry + 40, 170, 6, a.value / rl.target, tc);
        } else {
            ui.font.draw(c, rx2 - ui.font.measure(16, "Locked"), (int)by - 11, 16, "Locked", C_DIM);
        }

        int ax = ACH_X + 90, aw = rx - ptw - rightW - ax;
        std::string desc = (a.hidden && !a.unlocked) ? "Hidden achievement - keep playing!" : a.desc;
        int nw = ui.font.draw(c, ax, ry + 9, 19, a.name, a.unlocked ? C_TEXT : 0xFFC4C8D2, aw - (a.isNew ? 56 : 0));
        if (a.isNew) {
            int nx = ax + nw + 10;
            c.fillRound(nx, ry + 10, 46, 22, 11, C_GOLD);
            ui.font.draw(c, nx + 8, ry + 12, 15, "NEW", C_BG);
        }
        ui.font.draw(c, ax, ry + 36, 16, desc, C_SUB, aw);
    }
    c.resetClip();
    int n = (int)ui.achView.size();
    if (n > ACH_ROWS) {
        int track = ACH_H * ACH_ROWS - 16;
        int th = std::max(30, track * ACH_ROWS / n);
        int ty = ACH_Y + 8 + (track - th) * ui.achScroll / std::max(1, n - ACH_ROWS);
        c.fillRound(ACH_X + ACH_W - 9, ty, 4, th, 2, C_DIM);
    }
}

static void drawFooter(UI& ui) {
    Canvas& c = ui.cv;
    c.fillRect(0, 668, 1280, 1, C_LINE);
    int x = 28;
    if (ui.debug) { buttonHint(ui, x, 680, "B", "Close debug"); buttonHint(ui, x, 680, "+", "Exit"); return; }
    if (ui.upd || ui.cap || ui.pset) { buttonHint(ui, x, 680, "+", "Exit"); return; }
    bool game = ui.screen == SCR_GAME;
    if (ui.popup) buttonHint(ui, x, 680, "A", "Close");
    else if (game) { buttonHint(ui, x, 680, "B", "Back"); buttonHint(ui, x, 680, "L/R", "Filter"); }
    else { buttonHint(ui, x, 680, "A", "Open"); buttonHint(ui, x, 680, "L/R", "Sort"); }
    buttonHint(ui, x, 680, "Y", "Rescan");
    if (ui.sel != 0) buttonHint(ui, x, 680, "ZR", "Save captures");
    else if (!game) buttonHint(ui, x, 680, "ZR", "Compatibility scan");
    buttonHint(ui, x, 680, "X", "Debug");
    buttonHint(ui, x, 680, "-", game ? "Re-lock" : (ui.sel == 0 ? "Re-lock all" : "Re-lock game"));
    buttonHint(ui, x, 680, "ZL", "Update");
    buttonHint(ui, x, 680, "+", "Exit");
    std::string v = "v" APP_VERSION;
    ui.font.draw(c, 1256 - ui.font.measure(16, v), 684, 16, v, C_DIM);
}

// ---------------------------------------------------------------- classic look (the original layout, kept as an option)
static void classicHeader(UI& ui) {
    Canvas& c = ui.cv;
    c.gradientV(0, 0, 1280, 84, 0xFF1F2330, C_BG);
    c.fillCircle(52, 42, 26, 0xFF2A2F3D);
    drawTrophy(c, 52, 44, 30, C_GOLD);
    int tw = ui.font.draw(c, 92, 22, 30, "TrophyNX", C_TEXT);
    themeChip(ui, 92 + tw + 24, 24);
    std::string user = ui.font.ellipsize(22, plat::userName(), 300);
    int uw = ui.font.measure(22, user);
    ui.font.draw(c, 1256 - uw, 16, 22, user, C_TEXT);
    std::string stats = fmtInt(ui.lib.totalUnlocked) + " / " + fmtInt(ui.lib.totalCount) + " unlocked   " + fmtInt(ui.lib.totalPoints) + " pts";
    int sw = ui.font.measure(18, stats);
    ui.font.draw(c, 1256 - sw, 46, 18, stats, C_GOLD);
    c.fillRect(0, 84, 1280, 1, C_LINE);
}

static const int LIST_X = 24, LIST_Y = 100, LIST_W = 420, ROW_H = 78, LIST_ROWS = 7;
static const int DET_X = 464, DET_W = 792, CL_ACH_Y = 236, CL_ACH_H = 84, CL_ACH_ROWS = 5;

static void classicGameList(UI& ui) {
    Canvas& c = ui.cv;
    Library& lib = ui.lib;
    c.fillRound(LIST_X, LIST_Y - 4, LIST_W, ROW_H * LIST_ROWS + 8, 14, C_PANEL);
    c.setClip(LIST_X, LIST_Y - 4, LIST_W, ROW_H * LIST_ROWS + 8);
    for (int r = 0; r < LIST_ROWS; r++) {
        int gi = ui.listScroll + r;
        if (gi >= (int)lib.games.size()) break;
        const Game& g = lib.games[gi];
        int y = LIST_Y + r * ROW_H;
        bool selected = gi == ui.sel;
        if (selected) {
            c.fillRound(LIST_X + 6, y + 2, LIST_W - 12, ROW_H - 4, 10, C_PANEL_HI);
            if (!(ui.screen == SCR_GAME)) c.fillRound(LIST_X + 6, y + 14, 4, ROW_H - 28, 2, C_GOLD);
        }
        int ix = LIST_X + 18, iy = y + 11;
        if (gi == 0) {
            c.fillRound(ix, iy, 56, 56, 12, 0xFF2F3545);
            drawTrophy(c, ix + 28, iy + 30, 34, C_GOLD);
        } else if (const Image* img = gameIcon(ui, gi)) {
            c.blitRounded(*img, ix, iy, 10, g.stats.launches == 0);
        } else {
            c.fillRound(ix, iy, 56, 56, 12, 0xFF343A4A);
            std::string ch = g.name.substr(0, 1);
            ui.font.draw(c, ix + 28 - ui.font.measure(26, ch) / 2, iy + 12, 26, ch, C_SUB);
        }
        int tx = ix + 70;
        ui.font.draw(c, tx, y + 12, 20, g.name, g.stats.launches > 0 || gi == 0 ? C_TEXT : C_SUB, LIST_W - (tx - LIST_X) - 16);
        double frac = g.achs.empty() ? 0 : (double)g.unlocked / g.achs.size();
        progressBar(c, tx, y + 48, 200, 7, frac, frac >= 1 ? C_GREEN : C_GOLD);
        std::string cnt = std::to_string(g.unlocked) + "/" + std::to_string(g.achs.size());
        ui.font.draw(c, tx + 212, y + 41, 17, cnt, C_SUB);
        if (g.hasDefinition) {
            int bx = LIST_X + LIST_W - 30;
            c.fillCircle(bx, y + 52, 9, 0xFF3A3220);
            drawTrophy(c, bx, y + 53, 12, C_GOLD);
        }
    }
    c.resetClip();
    // scrollbar
    int n = (int)lib.games.size();
    if (n > LIST_ROWS) {
        int track = ROW_H * LIST_ROWS - 16;
        int th = std::max(30, track * LIST_ROWS / n);
        int ty = LIST_Y + 4 + (track - th) * ui.listScroll / std::max(1, n - LIST_ROWS);
        c.fillRound(LIST_X + LIST_W - 7, ty, 4, th, 2, C_DIM);
    }
}

static void classicDetails(UI& ui) {
    Canvas& c = ui.cv;
    Library& lib = ui.lib;
    const Game& g = lib.games[ui.sel];
    c.fillRound(DET_X, LIST_Y - 4, DET_W, ROW_H * LIST_ROWS + 8, 14, C_PANEL);

    int x = DET_X + 24;
    std::string title = ui.sel == 0 ? "Overview" : g.name;
    ui.font.draw(c, x, LIST_Y + 12, 26, title, C_TEXT, DET_W - 200);

    std::string line = std::to_string(g.unlocked) + " of " + std::to_string(g.achs.size()) + " unlocked   " + fmtInt(g.points) + " / " + fmtInt(g.maxPoints) + " pts";
    if (ui.sel == 0) {
        int played = 0;
        double hours = 0;
        for (size_t i = 1; i < lib.games.size(); i++) {
            if (lib.games[i].stats.launches > 0) played++;
            hours += lib.games[i].stats.playtimeNs / 3.6e12;
        }
        line += "   " + std::to_string(played) + " games played   " + fmtInt((long long)hours) + " h";
    } else if (g.stats.valid) {
        line += "   " + fmtNum(floor(g.stats.playtimeNs / 3.6e11) / 10.0) + " h played";
    }
    ui.font.draw(c, x, LIST_Y + 50, 18, line, C_SUB, DET_W - 200);

    double frac = g.achs.empty() ? 0 : (double)g.unlocked / g.achs.size();
    std::string pct = std::to_string((int)floor(frac * 100)) + "%";
    int pw = ui.font.measure(40, pct);
    ui.font.draw(c, DET_X + DET_W - 24 - pw, LIST_Y + 14, 40, pct, frac >= 1 ? C_GREEN : C_GOLD);
    progressBar(c, x, LIST_Y + 86, DET_W - 48, 8, frac, frac >= 1 ? C_GREEN : C_GOLD);

    // save status line
    std::string status;
    uint32_t scol = C_SUB;
    if (ui.sel == 0) {
        if (!lib.loadErrors.empty()) { status = "Some definition files have errors (press X)"; scol = C_RED; }
        else if (lib.liveStatus.find("running") == 0 || lib.liveStatus.find("started now") != std::string::npos) { status = "Live module: " + lib.liveStatus; scol = C_GREEN; }
        else { status = "Live module: " + lib.liveStatus; scol = lib.liveStatus.find("not installed") == 0 ? C_SUB : C_RED; }
    } else if (g.hasDefinition && g.reader.empty()) {
        status = "Themed play-time set (save format not supported yet)";
    } else if (g.hasDefinition) {
        switch (g.saveStatus) {
            case SaveStatus::Ok: status = "Save data read"; scol = C_GREEN; break;
            case SaveStatus::NoSave: status = "No save data for this user yet (if the game is suspended, close it and press Y)"; scol = C_GOLD; break;
            case SaveStatus::Unreadable: status = "Save found but not readable (unsupported version?)"; scol = C_RED; break;
            case SaveStatus::NeedsKey: status = "Encrypted save: needs its password once (X for how, or imp3r1us.github.io/TrophyNX/#key)"; scol = C_GOLD; break;
            default: break;
        }
    } else {
        status = "Play-time achievements only (no game-specific set yet)";
    }
    ui.font.draw(c, x, LIST_Y + 104, 16, status, scol, DET_W - 48);
    c.fillRect(DET_X + 16, CL_ACH_Y - 8, DET_W - 32, 1, C_LINE);

    c.setClip(DET_X, CL_ACH_Y - 6, DET_W, CL_ACH_H * CL_ACH_ROWS + 6);
    for (int r = 0; r < CL_ACH_ROWS; r++) {
        int ai = ui.achScroll + r;
        if (ai >= (int)ui.achView.size()) break;
        const Achievement& a = g.achs[ui.achView[ai]];
        int y = CL_ACH_Y + r * CL_ACH_H;
        if ((ui.screen == SCR_GAME) && ai == ui.achSel) {
            c.fillRound(DET_X + 10, y, DET_W - 20, CL_ACH_H - 6, 10, C_PANEL_HI);
            c.fillRound(DET_X + 10, y + 14, 4, CL_ACH_H - 34, 2, C_GOLD);
        }
        float bx = DET_X + 52, by = y + 38;
        uint32_t tc = tierColor(a);
        if (a.unlocked) {
            c.fillCircle(bx, by, 27, tc);
            c.fillCircle(bx, by, 22, 0xFF1B1E26);
            drawTrophy(c, bx, by + 2, 26, tc);
        } else {
            c.fillCircle(bx, by, 27, 0xFF2C303B);
            drawLock(c, bx, by + 2, 26, C_DIM);
        }
        int tx = DET_X + 96;
        int rightW = 230;
        std::string name = a.name;
        std::string desc = (a.hidden && !a.unlocked) ? "Hidden achievement - keep playing!" : a.desc;
        int nw = ui.font.draw(c, tx, y + 12, 21, name, a.unlocked ? C_TEXT : 0xFFC4C8D2, DET_W - (tx - DET_X) - rightW - 70);
        if (a.isNew) {
            int px = tx + nw + 10;
            c.fillRound(px, y + 13, 46, 22, 11, C_GOLD);
            ui.font.draw(c, px + 8, y + 15, 15, "NEW", C_BG);
        }
        ui.font.draw(c, tx, y + 44, 17, desc, C_SUB, DET_W - (tx - DET_X) - rightW - 10);

        int rx = DET_X + DET_W - 24;
        std::string pts = std::to_string(a.points) + " pts";
        int ptw = ui.font.measure(16, pts);
        if (a.unlocked) {
            std::string d = fmtDate(a.unlockTime);
            int dw = ui.font.measure(17, d);
            c.fillCircle(rx - dw - 16, y + 24, 9, C_GREEN);
            drawCheck(c, rx - dw - 16, y + 24, 16, C_BG);
            ui.font.draw(c, rx - dw, y + 14, 17, d, C_GREEN);
            ui.font.draw(c, rx - ptw, y + 46, 16, pts, tc);
        } else {
            const Rule& rl = a.rule;
            if (rl.showProgress && !(a.hidden)) {
                double cur = std::min(a.value, rl.target) / rl.divisor, tgt = rl.target / rl.divisor;
                std::string p = fmtNum(floor(cur * 10) / 10) + " / " + fmtNum(tgt);
                int pw2 = ui.font.measure(17, p);
                ui.font.draw(c, rx - pw2, y + 12, 17, p, C_TEXT);
                progressBar(c, rx - 170, y + 38, 170, 6, rl.target > 0 ? a.value / rl.target : 0, tc);
            } else {
                ui.font.draw(c, rx - ui.font.measure(17, "Locked"), y + 12, 17, "Locked", C_DIM);
            }
            ui.font.draw(c, rx - ptw, y + 52, 16, pts, C_DIM);
        }
    }
    c.resetClip();
    int n = (int)g.achs.size();
    if (n > CL_ACH_ROWS) {
        int track = CL_ACH_H * CL_ACH_ROWS - 12;
        int th = std::max(30, track * CL_ACH_ROWS / n);
        int ty = CL_ACH_Y + (track - th) * ui.achScroll / std::max(1, n - CL_ACH_ROWS);
        c.fillRound(DET_X + DET_W - 7, ty, 4, th, 2, C_DIM);
    }
}

static void classicFooter(UI& ui) {
    Canvas& c = ui.cv;
    c.fillRect(0, 668, 1280, 1, C_LINE);
    int x = 28;
    if (ui.debug) { buttonHint(ui, x, 680, "B", "Close debug"); buttonHint(ui, x, 680, "+", "Exit"); return; }
    if (ui.upd || ui.cap || ui.pset) { buttonHint(ui, x, 680, "+", "Exit"); return; }
    if (ui.popup) buttonHint(ui, x, 680, "A", "Close");
    else if (ui.screen == SCR_GAME) buttonHint(ui, x, 680, "B", "Back to games");
    else buttonHint(ui, x, 680, "A", "Open");
    buttonHint(ui, x, 680, "Y", "Rescan saves");
    if (ui.sel == 0 && !(ui.screen == SCR_GAME)) buttonHint(ui, x, 680, "ZR", "Compatibility scan");
    if (ui.sel != 0) buttonHint(ui, x, 680, "ZR", "Save captures");
    buttonHint(ui, x, 680, "X", "Debug");
    buttonHint(ui, x, 680, "-", (ui.screen == SCR_GAME) ? "Re-lock" : (ui.sel == 0 ? "Re-lock all" : "Re-lock game"));
    buttonHint(ui, x, 680, "ZL", "Update");
    buttonHint(ui, x, 680, "+", "Exit");
    std::string v = "v" APP_VERSION;
    ui.font.draw(c, 1256 - ui.font.measure(16, v), 684, 16, v, C_DIM);
}

static void drawPopup(UI& ui) {
    Canvas& c = ui.cv;
    Library& lib = ui.lib;
    c.fillRect(0, 0, 1280, 720, 0xB0000000);
    int n = (int)lib.newlyUnlocked.size();
    int shown = std::min(n, 6);
    int h = 130 + shown * 62 + (n > shown ? 30 : 0);
    int w = 720, x = (1280 - w) / 2, y = (720 - h) / 2;
    c.fillRound(x, y, w, h, 18, C_PANEL);
    c.fillRound(x, y, w, 6, 3, C_GOLD);
    drawTrophy(c, x + 48, y + 52, 40, C_GOLD);
    std::string t = n == 1 ? "Achievement unlocked!" : std::to_string(n) + " achievements unlocked!";
    ui.font.draw(c, x + 84, y + 30, 28, t, C_TEXT);
    for (int i = 0; i < shown; i++) {
        const Game& g = lib.games[lib.newlyUnlocked[i].first];
        const Achievement& a = g.achs[lib.newlyUnlocked[i].second];
        int ry = y + 96 + i * 62;
        uint32_t tc = tierColor(a);
        c.fillCircle(x + 48, ry + 24, 20, tc);
        c.fillCircle(x + 48, ry + 24, 16, C_PANEL);
        drawTrophy(c, x + 48, ry + 26, 19, tc);
        ui.font.draw(c, x + 84, ry + 4, 20, a.name, C_TEXT, w - 220);
        ui.font.draw(c, x + 84, ry + 30, 16, lib.newlyUnlocked[i].first == 0 ? "Overview" : g.name, C_SUB, w - 220);
        std::string p = "+" + std::to_string(a.points);
        ui.font.draw(c, x + w - 36 - ui.font.measure(20, p), ry + 12, 20, p, tc);
    }
    if (n > shown) ui.font.draw(c, x + 84, y + 96 + shown * 62, 17, "...and " + std::to_string(n - shown) + " more", C_SUB);
}

static const int DBG_LINES = 22;

static void drawDebug(UI& ui) {
    Canvas& c = ui.cv;
    const Game& g = ui.lib.games[ui.sel];
    c.fillRect(0, 0, 1280, 720, 0xC0000000);
    int x = 60, y = 40, w = 1160, h = 616;
    c.fillRound(x, y, w, h, 16, C_PANEL);
    c.fillRound(x, y, w, 5, 2, 0xFF8FE3F5);
    ui.font.draw(c, x + 24, y + 18, 24, "Debug - " + (ui.sel == 0 ? std::string("Overview") : g.name), C_TEXT, w - 260);
    std::string hint = "also in /switch/TrophyNX/log.txt";
    ui.font.draw(c, x + w - 24 - ui.font.measure(15, hint), y + 26, 15, hint, C_SUB);
    c.fillRect(x + 16, y + 60, w - 32, 1, C_LINE);
    int n = (int)g.debug.size();
    for (int i = 0; i < DBG_LINES; i++) {
        int li = ui.debugScroll + i;
        if (li >= n) break;
        const std::string& l = g.debug[li];
        uint32_t col = C_TEXT;
        if (l.find("[MET]") != std::string::npos) col = C_GREEN;
        if (l.find("NEW UNLOCK") != std::string::npos || l.find("UNLOCKED") != std::string::npos) col = C_GOLD;
        if (l.find("not present") != std::string::npos || l.find("[not yet]") != std::string::npos) col = C_SUB;
        if (l.find("!!") != std::string::npos || l.find("ERROR") != std::string::npos || l.find("failed") != std::string::npos ||
            l.find("NOT RECOGNISED") != std::string::npos) col = C_RED;
        ui.font.draw(c, x + 28, y + 74 + i * 24, 17, l, col, w - 70);
    }
    if (n > DBG_LINES) {
        int track = DBG_LINES * 24;
        int th = std::max(30, track * DBG_LINES / n);
        int ty = y + 74 + (track - th) * ui.debugScroll / std::max(1, n - DBG_LINES);
        c.fillRound(x + w - 14, ty, 4, th, 2, C_DIM);
    }
}

static void drawConfirm(UI& ui) {
    Canvas& c = ui.cv;
    const Game& g = ui.lib.games[ui.sel];
    c.fillRect(0, 0, 1280, 720, 0xB0000000);
    int w = 700, h = 230, x = (1280 - w) / 2, y = (720 - h) / 2;
    c.fillRound(x, y, w, h, 18, C_PANEL);
    c.fillRound(x, y, w, 6, 3, C_RED);
    std::string title, line1, line2;
    if (ui.confirm == 1) {
        title = "Re-lock this achievement?";
        line1 = g.achs[ui.relockAch].name + "  (" + (ui.sel == 0 ? std::string("Overview") : g.name) + ")";
    } else if (ui.confirm == 2) {
        title = "Re-lock every achievement of this game?";
        line1 = g.name + "  (" + std::to_string(g.unlocked) + " unlocked)";
    } else {
        title = "Re-lock ALL achievements for this user?";
        line1 = std::to_string(ui.lib.totalUnlocked) + " unlocked achievements will be cleared";
    }
    line2 = "They unlock again at the next scan (Y) if the save still qualifies.";
    ui.font.draw(c, x + 32, y + 32, 26, title, C_TEXT, w - 64);
    ui.font.draw(c, x + 32, y + 82, 19, line1, C_GOLD, w - 64);
    ui.font.draw(c, x + 32, y + 116, 17, line2, C_SUB, w - 64);
    int bx = x + 32;
    buttonHint(ui, bx, y + 170, "A", "Re-lock");
    buttonHint(ui, bx, y + 170, "B", "Cancel");
}

static std::string fmtSize(uint64_t b) {
    char t[32];
    if (b >= 1024 * 1024) snprintf(t, sizeof(t), "%.1f MB", b / 1048576.0);
    else snprintf(t, sizeof(t), "%llu KB", (unsigned long long)((b + 1023) / 1024));
    return t;
}

static std::string fileLabel(const std::string& path) {
    if (path == "switch/TrophyNX.nro") return "app";
    if (path.find("/exefs.nsp") != std::string::npos) return "live module";
    size_t s = path.find_last_of('/');
    return s == std::string::npos ? path : path.substr(s + 1);
}

static void drawUpdate(UI& ui) {
    Canvas& c = ui.cv;
    const UpdateInfo& u = ui.updInfo;
    c.fillRect(0, 0, 1280, 720, 0xB0000000);
    // room for "What's new" (CHANGELOG.md bullets) when an update is available
    size_t notes = (ui.upd != 2 && u.state == UpdateInfo::Available) ? u.notes.size() : 0;
    if (notes > 7) notes = 7;
    int w = 960, h = 380 + (notes ? 34 + 34 * (int)notes : 0), x = (1280 - w) / 2, y = (720 - h) / 2 - 20;
    if (h > 680) h = 680;
    if (y < 10) y = 10;
    c.fillRound(x, y, w, h, 18, C_PANEL);
    std::string title;
    uint32_t accent = C_GOLD;
    std::vector<std::pair<std::string, uint32_t>> lines;   // text, colour ('\n' splits)
    std::vector<std::pair<const char*, const char*>> btns;
    std::string build = (u.fromRelease ? "Release " + u.source.substr(u.source.find(' ') + 1) : (u.commit.empty() ? std::string("") : "Test build " + u.commit.substr(0, 7))) +
                        (u.date.empty() ? "" : "  ·  " + u.date);
    if (ui.upd == 2) {
        title = ui.updOk ? "Update installed" : "Update failed";
        accent = ui.updOk ? C_GREEN : C_RED;
        lines.push_back({ui.updResult, C_TEXT});
        if (u.appChanged) {   // the app released its own file to replace it: it has to restart
            lines.push_back({ui.updOk ? "Restart the app to use the new version." : "Restart the app (or reboot) to finish.", C_SUB});
            btns.push_back({"A", "Restart the app"});
            btns.push_back({"+", "Exit"});
        } else btns.push_back({"B", "Close"});
    } else if (u.state == UpdateInfo::Error) {
        title = "Update check failed";
        accent = C_RED;
        lines.push_back({u.error, C_TEXT});
        btns.push_back({"B", "Close"});
    } else if (u.state == UpdateInfo::UpToDate) {
        title = "You're up to date";
        accent = C_GREEN;
        lines.push_back({build, C_SUB});
        if (!u.message.empty()) lines.push_back({u.message, C_SUB});
        btns.push_back({"B", "Close"});
    } else {
        title = "Update available";
        lines.push_back({build, C_GOLD});
        if (!u.message.empty()) lines.push_back({u.message, C_TEXT});
        std::string vers;
        if (!u.appVersion.empty()) vers = "App " + u.appVersion;
        if (!u.moduleVersion.empty()) vers += (vers.empty() ? "" : "  ·  ") + std::string("Live module ") + u.moduleVersion;
        if (!vers.empty()) lines.push_back({vers, C_SUB});
        if (notes) {
            lines.push_back({"What's new:", C_GOLD});
            for (size_t i = 0; i < notes; i++) lines.push_back({"\xC2\xB7 " + u.notes[i], C_TEXT});
        }
        std::string ch;
        for (const UpdateFile& f : u.files)
            if (f.changed) ch += (ch.empty() ? "" : ", ") + fileLabel(f.path) + " (" + fmtSize(f.size) + ")";
        lines.push_back({"Changes: " + ch, C_SUB});
        if (u.moduleChanged) lines.push_back({"The live module is restarted after the update.", C_SUB});
        btns.push_back({"A", "Download and install"});
        btns.push_back({"B", "Later"});
    }
    c.fillRound(x, y, w, 6, 3, accent);
    ui.font.draw(c, x + 32, y + 30, 26, title, C_TEXT, w - 64);
    int ly = y + 84;
    for (auto& l : lines) {
        size_t a = 0;
        while (a <= l.first.size() && ly < y + h - 70) {
            size_t b = l.first.find('\n', a);
            std::string part = l.first.substr(a, b == std::string::npos ? std::string::npos : b - a);
            ui.font.draw(c, x + 32, ly, 18, part, l.second, w - 64);
            ly += 30;
            if (b == std::string::npos) break;
            a = b + 1;
        }
        ly += 4;
    }
    int bx = x + 32;
    for (auto& b : btns) buttonHint(ui, bx, y + h - 50, b.first, b.second);
}

static void openCaptures(UI& ui) {
    ui.capSet = loadCaptureSettings(plat::dataDir());
    ui.capList = listCaptures(plat::dataDir(), ui.lib.games[ui.sel].id);
    ui.capDelete = 0;
    ui.cap = 1;
}

static void startSharing(UI& ui) {
    ui.shareErr.clear();
    ui.shareIp.clear();
    std::string err;
    if (!plat::netInit(err)) { ui.shareErr = err; ui.cap = 2; return; }
    ui.shareIp = plat::localIp();
    if (ui.shareIp.empty()) { ui.shareErr = "The console is not connected to a network. Connect it to Wi-Fi first."; ui.cap = 2; return; }
    ui.server.dataDir = plat::dataDir();
    ui.server.pagePath = plat::romfsDir() + "/savelab.html";
    ui.server.names.clear();
    for (size_t i = 1; i < ui.lib.games.size(); i++) ui.server.names[ui.lib.games[i].id] = ui.lib.games[i].name;
    ui.server.requests = 0;
    ui.server.bytesSent = 0;
    ui.server.lastPath.clear();
    if (!ui.server.start(SHARE_PORT, err)) ui.shareErr = "Could not start the server: " + err;
    else plat::keepAwake(true);
    ui.cap = 2;
}

static void drawCaptures(UI& ui) {
    Canvas& c = ui.cv;
    const Game& g = ui.lib.games[ui.sel];
    c.fillRect(0, 0, 1280, 720, 0xB0000000);
    int w = 900, h = 440, x = (1280 - w) / 2, y = (720 - h) / 2 - 20;
    c.fillRound(x, y, w, h, 18, C_PANEL);
    int bx = x + 32;
    if (ui.cap == 2) {
        bool ok = ui.shareErr.empty();
        c.fillRound(x, y, w, 6, 3, ok ? C_GREEN : C_RED);
        ui.font.draw(c, x + 32, y + 30, 26, ok ? "Sharing over Wi-Fi" : "Wi-Fi sharing unavailable", C_TEXT, w - 64);
        if (!ok) {
            ui.font.draw(c, x + 32, y + 90, 19, ui.shareErr, C_RED, w - 64);
        } else {
            ui.font.draw(c, x + 32, y + 88, 19, "On your phone or computer, connected to the same Wi-Fi, open:", C_SUB, w - 64);
            std::string url = "http://" + ui.shareIp + ":" + std::to_string(SHARE_PORT);
            ui.font.draw(c, x + 32, y + 126, 40, url, C_GOLD, w - 64);
            ui.font.draw(c, x + 32, y + 196, 18, "The Save Lab opens with your captures: \"From your Switch\" at the top of the page.", C_TEXT, w - 64);
            ui.font.draw(c, x + 32, y + 226, 18, "Type the address exactly (http, not https). Keep this screen open while it loads.", C_SUB, w - 64);
            std::string st = std::to_string(ui.server.requests) + " file(s) sent, " + fmtSize(ui.server.bytesSent);
            ui.font.draw(c, x + 32, y + 290, 18, st, C_GREEN, w - 64);
            if (!ui.server.lastPath.empty()) ui.font.draw(c, x + 32, y + 318, 15, "last: " + ui.server.lastPath, C_DIM, w - 64);
        }
        buttonHint(ui, bx, y + h - 50, "B", ok ? "Stop sharing" : "Back");
        return;
    }
    bool on = captureEnabled(ui.capSet, g.id);
    c.fillRound(x, y, w, 6, 3, on ? C_GREEN : 0xFF8FE3F5);
    ui.font.draw(c, x + 32, y + 30, 26, "Save captures - " + g.name, C_TEXT, w - 64);
    ui.font.draw(c, x + 32, y + 84, 21, on ? "Recording: ON" : "Recording: off", on ? C_GREEN : C_SUB);
    uint64_t bytes = 0;
    for (const CaptureInfo& ci : ui.capList) bytes += ci.bytes;
    std::string n = ui.capList.empty() ? std::string("No capture yet.")
                                        : std::to_string(ui.capList.size()) + (ui.capList.size() == 1 ? " capture, " : " captures, ") + fmtSize(bytes) +
                                              "  ·  last: " + fmtDate(ui.capList.back().time) + "  ·  keeps the last " + std::to_string(ui.capSet.keep);
    ui.font.draw(c, x + 32, y + 120, 18, n, C_TEXT, w - 64);
    const char* lines[] = {
        "When recording is on, the live module copies this game's save each time you CLOSE the game.",
        "A game keeps its save locked while it runs, so closing it is when a copy can be taken:",
        "close it (HOME, then X) after each step worth comparing (a boss, a new item, a chapter...).",
        "Unchanged saves are not kept twice. Then share them over Wi-Fi to the Save Lab (Y).",
    };
    for (int i = 0; i < 4; i++) ui.font.draw(c, x + 32, y + 168 + i * 28, 17, lines[i], C_SUB, w - 64);
    if (ui.capDelete) ui.font.draw(c, x + 32, y + 300, 18, "Press - again to delete every capture of this game.", C_RED, w - 64);
    buttonHint(ui, bx, y + h - 50, "A", on ? "Stop recording" : "Start recording");
    buttonHint(ui, bx, y + h - 50, "Y", "Share over Wi-Fi");
    if (!ui.capList.empty()) buttonHint(ui, bx, y + h - 50, "-", "Delete captures");
    buttonHint(ui, bx, y + h - 50, "B", "Close");
}

// ---------------------------------------------------------------- popup settings (right stick)
// Edits popup.json, which the live module reads before every popup. The preview is drawn by the module's own
// renderer (source/popup_draw.cpp), so it is exactly what appears in game.
enum { PS_CARD, PS_SHOW, PS_DURATION, PS_LIVE, PS_PROGRESS, PS_POSITION, PS_CTHEME, PS_ACCENT, PS_OPACITY, PS_BORDER,
       PS_ICON, PS_LABEL, PS_THEME, PS_ICONSIDE, PS_SIZE, PS_SOUND, PS_VOLUME };
static const char* PS_STYLES[] = {"custom", "both", "ultrahand", "off"};
static const char* PS_KINDS[] = {"Unlock", "Progress", "Live save check", "Already unlocked"};
static const int PS_GENERAL = 7;                          // rows before the card's own section
static const int MINI_W = 512, MINI_H = 288;              // the TV in the preview (1920x1080 scaled down)

static std::string popupJsonPath() { return plat::dataDir() + "/popup.json"; }

static std::vector<int> psRows(const PopupSettings& s) {
    std::vector<int> r = {PS_CARD, PS_SHOW, PS_DURATION, PS_LIVE, PS_PROGRESS, PS_SOUND, PS_VOLUME};
    if (s.compact) r.insert(r.end(), {PS_SIZE, PS_POSITION, PS_CTHEME, PS_ACCENT, PS_OPACITY, PS_BORDER, PS_ICON, PS_LABEL});
    else r.insert(r.end(), {PS_THEME, PS_ICONSIDE});
    return r;
}

static const char* psLabel(int row) {
    switch (row) {
        case PS_CARD: return "Card";
        case PS_SHOW: return "Show popups";
        case PS_DURATION: return "Duration";
        case PS_LIVE: return "Live save check";
        case PS_PROGRESS: return "Progress popups";
        case PS_SOUND: return "Unlock sound";
        case PS_VOLUME: return "Sound volume";
        case PS_POSITION: return "Position";
        case PS_SIZE: return "Size";
        case PS_CTHEME: return "Theme";
        case PS_ACCENT: return "Accent colour";
        case PS_OPACITY: return "Background opacity";
        case PS_BORDER: return "Border";
        case PS_ICON: return "Icon";
        case PS_LABEL: return "Label line";
        case PS_THEME: return "Theme";
        default: return "Icon side";
    }
}

static const char* psHelp(int row) {
    switch (row) {
        case PS_CARD: return "Classic: text bar + tier medal. Compact: small card in a corner.";
        case PS_SHOW: return "Ultrahand: also (or only) as an Ultrahand notification.";
        case PS_DURATION: return "Time on screen. Shorter when several popups wait.";
        case PS_LIVE: return "First read of a session: what live save read + achievements you have.";
        case PS_PROGRESS: return "Off: a popup only when an achievement unlocks. On: also when a counter goes up (5 / 10).";
        case PS_SOUND: return "Played with each unlock. My sound: a PCM .wav in switch/TrophyNX/sounds/unlock.wav (3 s max).";
        case PS_VOLUME: return "Loudness of the unlock sound.";
        case PS_POSITION: return "Where the card shows, 24 px from the edges. Centered cards drop in from the edge.";
        case PS_SIZE: return "How big the card is. Large is 1.5x the original small card.";
        case PS_CTHEME: return "Colours of the card and its text.";
        case PS_ACCENT: return "Colour of the label. By tier: cyan, gold for gold or secret, violet for platinum.";
        case PS_OPACITY: return "How much of the game shows through the card.";
        case PS_BORDER: return "The thin light line around the card.";
        case PS_ICON: return "The achievement's art or the game icon on the left.";
        case PS_LABEL: return "The small coloured line above the title (ACHIEVEMENT UNLOCKED...).";
        case PS_THEME: return "Dark: black bar, white text. Light: white bar, dark text.";
        default: return "Icon after the text, or at the screen edge.";
    }
}

static std::string psOnOff(bool b) { return b ? "On" : "Off"; }

static std::string psValue(const PopupSettings& s, int row) {
    switch (row) {
        case PS_CARD: return s.compact ? "Compact" : "Classic";
        case PS_SHOW: return popupStyleLabel(s.style);
        case PS_DURATION:
            if (!s.durationMs) return s.compact ? "Auto (3.3 s)" : "Auto (4 s)";
            return fmtNum(s.durationMs / 1000.0) + " s";
        case PS_LIVE: return psOnOff(s.liveCheck);
        case PS_PROGRESS: return s.progress ? "Every progress" : "Unlocks only";
        case PS_SOUND:
            for (const NamedOption& o : popupSounds()) if (s.sound == o.name) return o.label;
            return s.sound;
        case PS_VOLUME: return std::to_string(s.volume) + " %";
        case PS_POSITION: return popupCornerLabel(s.corner);
        case PS_SIZE:
            for (const NamedSize& z : popupToastSizes()) if (s.size == z.name) return z.label;
            return s.size;
        case PS_CTHEME:
            for (const NamedColor& t : popupToastThemes()) if (s.toastTheme == t.name) return t.label;
            return s.toastTheme;
        case PS_ACCENT:
            for (const NamedColor& a : popupAccents()) if (s.accent == a.name) return a.label;
            return s.accent;
        case PS_OPACITY: return std::to_string(s.opacity) + " %";
        case PS_BORDER: return psOnOff(s.border);
        case PS_ICON: return psOnOff(s.icon);
        case PS_LABEL: return psOnOff(s.label);
        case PS_THEME: return s.classicDark ? "Dark" : "Light";
        default: return s.classicIconLeft ? "Left" : "Right";
    }
}

static int psCycle(int i, int n, int dir) { return ((i + dir) % n + n) % n; }

static void psChange(PopupSettings& s, int row, int dir) {
    switch (row) {
        case PS_CARD: s.compact = !s.compact; break;
        case PS_SHOW: {
            int i = 0;
            for (int k = 0; k < 4; k++) if (s.style == PS_STYLES[k]) i = k;
            s.style = PS_STYLES[psCycle(i, 4, dir)];
            break;
        }
        case PS_DURATION: {
            const std::vector<int>& d = popupDurations();
            int i = 0;
            for (size_t k = 0; k < d.size(); k++) if (d[k] == s.durationMs) i = (int)k;
            s.durationMs = d[psCycle(i, (int)d.size(), dir)];
            break;
        }
        case PS_LIVE: s.liveCheck = !s.liveCheck; break;
        case PS_PROGRESS: s.progress = !s.progress; break;
        case PS_SOUND: {
            const auto& o = popupSounds();
            int i = 0;
            for (int k = 0; k < (int)o.size(); k++) if (s.sound == o[k].name) i = k;
            s.sound = o[psCycle(i, (int)o.size(), dir)].name;
            break;
        }
        case PS_VOLUME: s.volume = std::min(100, std::max(0, s.volume + dir * 10)); break;
        case PS_POSITION: {   // left to right, top row then bottom row
            static const int ORDER[] = {CORNER_TOP_LEFT, CORNER_TOP_CENTER, CORNER_TOP_RIGHT,
                                        CORNER_BOTTOM_LEFT, CORNER_BOTTOM_CENTER, CORNER_BOTTOM_RIGHT};
            int i = 0;
            for (int k = 0; k < 6; k++) if (ORDER[k] == s.corner) i = k;
            s.corner = ORDER[psCycle(i, 6, dir)];
            break;
        }
        case PS_SIZE: {
            const std::vector<NamedSize>& z = popupToastSizes();
            int i = 0;
            for (size_t k = 0; k < z.size(); k++) if (s.size == z[k].name) i = (int)k;
            s.size = z[psCycle(i, (int)z.size(), dir)].name;
            break;
        }
        case PS_CTHEME: {
            const std::vector<NamedColor>& t = popupToastThemes();
            int i = 0;
            for (size_t k = 0; k < t.size(); k++) if (s.toastTheme == t[k].name) i = (int)k;
            s.toastTheme = t[psCycle(i, (int)t.size(), dir)].name;
            break;
        }
        case PS_ACCENT: {
            const std::vector<NamedColor>& a = popupAccents();
            int i = 0;
            for (size_t k = 0; k < a.size(); k++) if (s.accent == a[k].name) i = (int)k;
            s.accent = a[psCycle(i, (int)a.size(), dir)].name;
            break;
        }
        case PS_OPACITY: s.opacity = std::max(50, std::min(100, s.opacity + 5 * dir)); break;
        case PS_BORDER: s.border = !s.border; break;
        case PS_ICON: s.icon = !s.icon; break;
        case PS_LABEL: s.label = !s.label; break;
        case PS_THEME: s.classicDark = !s.classicDark; break;
        default: s.classicIconLeft = !s.classicIconLeft; break;
    }
}

static Image psToImage(const PopupImage& p) {   // straight RGBA8 -> the canvas' packed RGBA
    Image im;
    im.w = p.w;
    im.h = p.h;
    im.px.resize((size_t)p.w * p.h);
    for (size_t i = 0; i < im.px.size(); i++) {
        const uint8_t* s = &p.px[i * 4];
        im.px[i] = (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
    }
    return im;
}

static PopupImage psFromImage(const Image& im) {
    PopupImage p;
    p.w = im.w;
    p.h = im.h;
    p.px.resize((size_t)im.w * im.h * 4);
    for (size_t i = 0; i < im.px.size(); i++) {
        uint32_t v = im.px[i];
        p.px[i * 4] = v & 255; p.px[i * 4 + 1] = (v >> 8) & 255; p.px[i * 4 + 2] = (v >> 16) & 255; p.px[i * 4 + 3] = v >> 24;
    }
    return p;
}

// A sample popup of the selected game (its first achievement, or one with a counter for "Progress").
static PopupText psSample(UI& ui, int kind) {
    PopupText t;
    t.kind = kind;
    const Game* g = ui.sel > 0 ? &ui.lib.games[ui.sel] : nullptr;
    t.game = g ? g->name : "Hades";
    const Achievement* a = nullptr;
    if (g)
        for (const Achievement& x : g->achs)
            if (kind != POPUP_PROGRESS || (x.rule.showProgress && x.rule.target > 1 && !x.hidden)) { a = &x; break; }
    if (a) {
        t.title = a->name; t.desc = a->desc; t.points = a->points; t.tier = tierOf(*a); t.hidden = a->hidden;
    } else {
        t.title = kind == POPUP_PROGRESS ? "Pet Cerberus" : "Escaped Tartarus";
        t.desc = kind == POPUP_PROGRESS ? "Pet Cerberus 10 times" : "Clear Tartarus";
        t.points = 10;
    }
    if (kind == POPUP_PROGRESS) {
        if (a) {
            double tgt = a->rule.target / a->rule.divisor;
            t.line = fmtNum(std::floor(tgt / 2)) + " / " + fmtNum(tgt);
        } else t.line = "5 / 10";
    } else if (kind == POPUP_LIVE_CHECK) {
        int n = g ? (int)g->achs.size() : 41, met = g ? g->unlocked : 3;
        t.line = std::to_string(met) + "/" + std::to_string(n) + " met \xC2\xB7 read in 80 ms";
    } else if (kind == POPUP_KNOWN) t.line = "met in your save";
    return t;
}

static void psRenderPreview(UI& ui) {
    std::string key = popupSettingsJson(ui.ps) + std::to_string(ui.psKind) + "/" + std::to_string(ui.sel);
    if (key == ui.psKey) return;
    ui.psKey = key;
    PopupText t = psSample(ui, ui.psKind);
    PopupImage icon;
    const Image* gi = ui.sel > 0 ? gameIcon(ui, ui.sel) : nullptr;
    if (gi && gi->valid()) icon = psFromImage(*gi);
    PopupImage card;
    int x, y;
    if (ui.ps.compact) {
        ToastInfo ti = popupToastTexts(t, ui.ps);
        ti.icon = icon.w ? &icon : nullptr;
        popupRenderToast(ti, card, popupToastStyle(ui.ps));
        int lw, lh;
        popupToastLayer(ui.ps.corner, card.w, card.h, x, y, lw, lh);
        x += popupToastCardX(ui.ps.corner, 1.f);
        y += popupToastCardY(ui.ps.corner, 1.f);
        PopupImage s;   // its real size on this 720p screen (the layer is in 1080p pixels)
        popupScaleImage(card.px.data(), card.w, card.h, card.w * 2 / 3, card.h * 2 / 3, s);
        ui.psBig = psToImage(s);
    } else {
        PopupInfo pi;
        popupClassicTexts(t, pi.title, pi.desc);
        pi.tier = t.tier;
        pi.dark = ui.ps.classicDark;
        pi.iconLeft = ui.ps.classicIconLeft;
        pi.icon = icon.w ? &icon : nullptr;
        popupRenderCard(pi, card);
        x = 0;
        y = (int)(POPUP_Y720 * POPUP_SCALE + 0.5f);
        PopupImage s;
        popupScaleImage(card.px.data(), card.w, card.h, card.w * 2 / 3, card.h * 2 / 3, s);   // its real size on 720p
        ui.psBig = psToImage(s);
    }
    const float k = MINI_W / 1920.f;
    PopupImage m;
    popupScaleImage(card.px.data(), card.w, card.h, std::max(1, (int)lroundf(card.w * k)), std::max(1, (int)lroundf(card.h * k)), m);
    ui.psMini = psToImage(m);
    ui.psMiniX = (int)lroundf(x * k);
    ui.psMiniY = (int)lroundf(y * k);
}

static void openPopupSettings(UI& ui) {
    ui.ps = PopupSettings();
    ui.psWarn.clear();
    std::vector<uint8_t> b;
    if (readFile(popupJsonPath(), b)) {
        b.push_back(0);
        if (!popupSettingsParse((const char*)b.data(), ui.ps, &ui.psWarn)) {
            ui.ps = PopupSettings();
            ui.psWarn = "popup.json is not valid JSON: these are the defaults.";
        }
    }
    ui.psSel = 0;
    ui.psChanged = false;
    ui.psKey.clear();
    ui.pset = true;
}

static void drawPopupSettings(UI& ui) {
    Canvas& c = ui.cv;
    psRenderPreview(ui);
    c.fillRect(0, 0, 1280, 720, 0xC8000000);
    const int x = 40, y = 26, w = 1200, h = 636;
    c.fillRound(x, y, w, h, 18, C_PANEL);
    c.fillRound(x, y, w, 6, 3, 0xFF38BDF8);
    ui.font.draw(c, x + 32, y + 24, 26, "Popup settings", C_TEXT);
    ui.font.draw(c, x + 32, y + 62, 16, "What the live module shows in game. Used from the next popup.", C_SUB, 560);

    // settings list
    std::vector<int> rows = psRows(ui.ps);
    int ry = y + 104;
    const int step = rows.size() > 13 ? 26 : 30;   // compact card: 15 rows must end above the help line
    for (size_t i = 0; i < rows.size(); i++) {
        if ((int)i == PS_GENERAL) {   // the card's own section
            ry += 8;
            ui.font.draw(c, x + 36, ry, 14, ui.ps.compact ? "COMPACT CARD" : "CLASSIC CARD", C_DIM);
            ry += 24;
        }
        bool sel = (int)i == ui.psSel;
        if (sel) c.fillRound(x + 22, ry - 5, 588, step - 1, 8, C_PANEL_HI);
        ui.font.draw(c, x + 36, ry, 17, psLabel(rows[i]), sel ? C_TEXT : C_SUB);
        std::string v = psValue(ui.ps, rows[i]);
        int vx = x + 596 - ui.font.measure(17, v) - (sel ? 22 : 0);
        if (rows[i] == PS_ACCENT || rows[i] == PS_CTHEME) {   // colour swatch
            uint32_t col = rows[i] == PS_ACCENT ? popupAccent(ui.ps, TIER_GOLD, false) : 0;
            if (rows[i] == PS_ACCENT && ui.ps.accent == "tier") col = ACCENT_CYAN;
            if (rows[i] == PS_CTHEME)
                for (const NamedColor& t : popupToastThemes()) if (ui.ps.toastTheme == t.name) col = t.rgb;
            c.fillCircle(vx - 14, ry + 11, 7, 0xFF000000 | col);
            c.ring(vx - 14, ry + 11, 7, 1, 0x40FFFFFF);
        }
        ui.font.draw(c, vx, ry, 17, v, sel ? C_GOLD : C_TEXT);
        if (sel) {
            ui.font.draw(c, vx - (rows[i] == PS_ACCENT || rows[i] == PS_CTHEME ? 40 : 18), ry, 17, "<", C_DIM);
            ui.font.draw(c, x + 584, ry, 17, ">", C_DIM);
        }
        ry += step;
    }
    if (ui.psSel >= 0 && ui.psSel < (int)rows.size())
        ui.font.draw(c, x + 32, y + h - 104, 15, psHelp(rows[ui.psSel]), C_SUB, 580);
    if (!ui.psWarn.empty()) ui.font.draw(c, x + 32, y + h - 80, 14, ui.psWarn, C_RED, 580);

    // preview: the TV with the card where it appears, then the card itself
    const int px = x + 648, py = y + 104;
    ui.font.draw(c, px, py - 34, 17, std::string("Preview: ") + PS_KINDS[ui.psKind], C_TEXT);
    c.fillRound(px - 6, py - 6, MINI_W + 12, MINI_H + 12, 10, 0xFF0B0C10);
    c.gradientV(px, py, MINI_W, MINI_H, 0xFF3B6EA5, 0xFF7FB07A);   // a bright game scene
    c.fillRect(px, py + MINI_H - 70, MINI_W, 70, 0xFF4F7A3E);
    c.fillCircle(px + 400, py + 70, 26, 0xFFF6E7A6);
    c.blitAlpha(ui.psMini, px + ui.psMiniX, py + ui.psMiniY);
    int cy = py + MINI_H + 28;
    ui.font.draw(c, px, cy, 14, "Real size", C_DIM);
    cy += 24;
    c.gradientV(px - 6, cy - 6, MINI_W + 12, ui.psBig.h + 12, 0xFF2B3A55, 0xFF41573C);
    c.blitAlpha(ui.psBig, px, cy);

    int bx = x + 32;
    buttonHint(ui, bx, y + h - 46, "A", "Change");
    buttonHint(ui, bx, y + h - 46, "Y", "Next preview");
    buttonHint(ui, bx, y + h - 46, "X", "Defaults");
    buttonHint(ui, bx, y + h - 46, "-", "Cancel");
    buttonHint(ui, bx, y + h - 46, "B", ui.psChanged ? "Save & close" : "Close");
}

static void drawAll(UI& ui) {
    int stride;
    ui.cv.px = plat::beginDraw(stride);
    ui.cv.stride = stride;
    ui.cv.resetClip();
    ui.cv.clear(C_BG);
    if (ui.classic) { classicHeader(ui); classicGameList(ui); classicDetails(ui); }
    else {
        drawHeader(ui);
        if (ui.screen == SCR_GAME) drawGamePage(ui);
        else drawLibrary(ui);
    }
    if (ui.popup) drawPopup(ui);
    if (ui.debug) drawDebug(ui);
    if (ui.confirm) drawConfirm(ui);
    if (ui.upd) drawUpdate(ui);
    if (ui.cap) drawCaptures(ui);
    if (ui.pset) drawPopupSettings(ui);
    if (ui.classic) classicFooter(ui);
    else drawFooter(ui);
    if (!ui.toast.empty()) {
        int tw = ui.font.measure(18, ui.toast) + 40;
        ui.cv.fillRound(640 - tw / 2, 616, tw, 40, 20, 0xFF2F3545);
        ui.font.draw(ui.cv, 640 - tw / 2 + 20, 625, 18, ui.toast, C_TEXT);
    }
    plat::endDraw();
}

static void drawLoading(UI& ui, int done, int total) {
    int stride;
    ui.cv.px = plat::beginDraw(stride);
    ui.cv.stride = stride;
    ui.cv.resetClip();
    Canvas& c = ui.cv;
    c.clear(C_BG);
    c.fillCircle(640, 280, 56, 0xFF232733);
    drawTrophy(c, 640, 286, 68, C_GOLD);
    std::string t = "TrophyNX";
    ui.font.draw(c, 640 - ui.font.measure(34, t) / 2, 360, 34, t, C_TEXT);
    std::string s = std::string(ui.loadingLabel) + (total > 0 ? "  " + std::to_string(done) + " / " + std::to_string(total) : "");
    ui.font.draw(c, 640 - ui.font.measure(19, s) / 2, 412, 19, s, C_SUB);
    progressBar(c, 440, 450, 400, 8, total > 0 ? (double)done / total : 0, C_GOLD);
    plat::endDraw();
}

static void onProgress(int done, int total, void* ctx) {
    UI* ui = (UI*)ctx;
    int pct = total > 0 ? done * 50 / total : 0;  // redraw at most ~50 times
    if (pct == ui->lastProgress && done != total) return;
    ui->lastProgress = pct;
    drawLoading(*ui, done, total);
}

static void showFatal(UI& ui, const std::string& msg) {
    while (plat::poll()) {
        int stride;
        ui.cv.px = plat::beginDraw(stride);
        if (!ui.cv.px) break;
        ui.cv.stride = stride;
        ui.cv.resetClip();
        ui.cv.clear(C_BG);
        ui.font.draw(ui.cv, 80, 300, 28, "TrophyNX could not start", C_TEXT);
        ui.font.draw(ui.cv, 80, 350, 20, msg, C_RED);
        ui.font.draw(ui.cv, 80, 400, 18, "Press + to exit", C_SUB);
        plat::endDraw();
        if (plat::buttonsDown() & (plat::BTN_PLUS | plat::BTN_B)) break;
        plat::sleepMs(16);
    }
}

static void clampScroll(UI& ui) {
    if (ui.classic) {
        int n = (int)ui.lib.games.size();
        ui.sel = std::max(0, std::min(ui.sel, n - 1));
        if (ui.sel < ui.listScroll) ui.listScroll = ui.sel;
        if (ui.sel >= ui.listScroll + LIST_ROWS) ui.listScroll = ui.sel - LIST_ROWS + 1;
        int na = (int)ui.achView.size();
        ui.achSel = std::max(0, std::min(ui.achSel, std::max(0, na - 1)));
        if (ui.achSel < ui.achScroll) ui.achScroll = ui.achSel;
        if (ui.achSel >= ui.achScroll + CL_ACH_ROWS) ui.achScroll = ui.achSel - CL_ACH_ROWS + 1;
        ui.achScroll = std::max(0, std::min(ui.achScroll, std::max(0, na - CL_ACH_ROWS)));
        return;
    }
    int n = (int)ui.order.size();
    ui.gridPos = std::max(0, std::min(ui.gridPos, n - 1));
    ui.sel = ui.order[ui.gridPos];
    int row = ui.gridPos / COLS, rows = (n + COLS - 1) / COLS;
    if (row < ui.gridScroll) ui.gridScroll = row;
    if (row >= ui.gridScroll + GRID_ROWS) ui.gridScroll = row - GRID_ROWS + 1;
    ui.gridScroll = std::max(0, std::min(ui.gridScroll, std::max(0, rows - GRID_ROWS)));
    int na = (int)ui.achView.size();
    ui.achSel = std::max(0, std::min(ui.achSel, std::max(0, na - 1)));
    if (ui.achSel < ui.achScroll) ui.achScroll = ui.achSel;
    if (ui.achSel >= ui.achScroll + ACH_ROWS) ui.achScroll = ui.achSel - ACH_ROWS + 1;
    ui.achScroll = std::max(0, std::min(ui.achScroll, std::max(0, na - ACH_ROWS)));
}

int main(int argc, char** argv) {
    static UI ui;
    g_ui = &ui;
    if (argc > 0 && argv[0]) plat::setSelfPath(argv[0]);   // hbloader passes the .nro path
    std::string err;
    bool ok = plat::init(err);
    const uint8_t *f0 = nullptr, *f1 = nullptr;
    size_t s0 = 0, s1 = 0;
    plat::fontData(0, &f0, &s0);
    plat::fontData(1, &f1, &s1);
    if (f0) ui.font.load(f0, s0, f1, s1);
    if (f0) popupFontInit(f0);   // the popup settings preview uses the live module's renderer
    if (!ok) {
        if (err != "No user selected.") showFatal(ui, err);
        plat::exit();
        return 0;
    }

    // The app was called "Switch Achievements" before 0.9.0: keep the unlocks, settings and token
    std::string moved = migrateLegacyDir(plat::sdRoot() + "switch/SwitchAchievements", plat::dataDir());
    std::string self = plat::selfPath(), oldNro = plat::sdRoot() + "switch/SwitchAchievements.nro";
    if (self.size() >= 12 && self.compare(self.size() - 12, 12, "TrophyNX.nro") == 0 && self != oldNro && pathExists(oldNro))
        remove(oldNro.c_str());   // installed by the old updater next to the old name: one app entry only
    drawLoading(ui, 0, 0);
    buildLibrary(ui.lib, onProgress, &ui);
    if (!moved.empty()) ui.toast = "Renamed to TrophyNX: your achievements were kept.";
    ui.popup = !ui.lib.newlyUnlocked.empty();
    loadLook(ui);
    clampScroll(ui);
    if (!ui.lib.loadErrors.empty()) {
        makeDirs(plat::dataDir());
        writeFile(plat::dataDir() + "/definition_errors.txt", ui.lib.loadErrors);
    }

    bool dirty = true;
    int holdFrames = 0;
    uint32_t lastHeld = 0;
    while (plat::poll()) {
        uint32_t down = plat::buttonsDown(), held = plat::buttonsHeld();
        // key repeat for up/down
        uint32_t dirs = held & (plat::BTN_UP | plat::BTN_DOWN | plat::BTN_LEFT | plat::BTN_RIGHT);
        if (dirs && dirs == lastHeld) {
            if (++holdFrames > 24 && holdFrames % 5 == 0) down |= dirs;
        } else holdFrames = 0;
        lastHeld = dirs;

        if (down & plat::BTN_PLUS) break;
        if (down && !ui.toast.empty()) { ui.toast.clear(); dirty = true; }
        if (ui.pset) {
            if (down) dirty = true;
            std::vector<int> rows = psRows(ui.ps);
            int n = (int)rows.size();
            if (down & plat::BTN_UP) ui.psSel = (ui.psSel + n - 1) % n;
            if (down & plat::BTN_DOWN) ui.psSel = (ui.psSel + 1) % n;
            int dir = (down & (plat::BTN_RIGHT | plat::BTN_A)) ? 1 : (down & plat::BTN_LEFT) ? -1 : 0;
            if (dir) {
                psChange(ui.ps, rows[ui.psSel], dir);
                ui.psChanged = true;
                ui.psSel = std::min(ui.psSel, (int)psRows(ui.ps).size() - 1);   // the card's section changed size
            }
            if (down & plat::BTN_Y) ui.psKind = (ui.psKind + 1) % 4;
            if (down & plat::BTN_X) { ui.ps = PopupSettings(); ui.psChanged = true; ui.psSel = 0; ui.toast = "Defaults restored: B to save."; }
            if (down & plat::BTN_MINUS) { ui.pset = false; ui.toast = "Popup settings unchanged."; }
            else if (down & plat::BTN_B) {
                ui.pset = false;
                if (ui.psChanged) {
                    makeDirs(plat::dataDir());
                    ui.toast = writeFile(popupJsonPath(), popupSettingsJson(ui.ps)) ? "Popup settings saved: the next popup uses them."
                                                                                     : "Could not write popup.json";
                }
            }
        } else if (ui.cap == 2) {
            if (ui.server.running() && ui.server.poll() > 0) dirty = true;
            if (down & plat::BTN_B) { ui.server.stop(); plat::keepAwake(false); ui.cap = 1; ui.capList = listCaptures(plat::dataDir(), ui.lib.games[ui.sel].id); dirty = true; }
        } else if (ui.cap == 1) {
            if (down) dirty = true;
            const Game& g = ui.lib.games[ui.sel];
            if (down & plat::BTN_MINUS) {
                if (ui.capList.empty()) {}
                else if (ui.capDelete) {
                    int n = deleteCaptures(plat::dataDir(), g.id);
                    ui.capList.clear();
                    ui.capDelete = 0;
                    ui.toast = std::to_string(n) + " capture(s) deleted.";
                } else ui.capDelete = 1;
            } else if (down) ui.capDelete = 0;
            if (down & plat::BTN_A) {
                bool on = !captureEnabled(ui.capSet, g.id);
                setCaptureEnabled(ui.capSet, g.id, on);
                if (!saveCaptureSettings(plat::dataDir(), ui.capSet)) ui.toast = "Could not write captures/settings.json";
                else ui.toast = on ? "Recording on: close the game to take a capture." : "Recording off (captures are kept).";
            }
            if (down & plat::BTN_Y) startSharing(ui);
            else if (down & plat::BTN_B) ui.cap = 0;
        } else if (ui.upd) {
            if (ui.upd == 1 && ui.updInfo.state == UpdateInfo::Available && (down & plat::BTN_A)) {
                ui.loadingLabel = "Downloading the update (KB)";
                ui.lastProgress = -1;
                drawLoading(ui, 0, 0);
                ui.updOk = applyUpdate(ui.updInfo, onProgress, &ui, ui.updResult);
                ui.upd = 2;
                dirty = true;
            } else if (ui.upd == 2 && ui.updInfo.appChanged && (down & plat::BTN_A)) {
                plat::relaunchSelf();
                break;
            } else if ((down & plat::BTN_B) && !(ui.upd == 2 && ui.updInfo.appChanged)) { ui.upd = 0; dirty = true; }
        } else if (ui.confirm) {
            if (down & plat::BTN_A) {
                int n = 0;
                if (ui.confirm == 1) n = relockAchievements(ui.lib, ui.sel, ui.relockAch);
                else if (ui.confirm == 2) n = relockAchievements(ui.lib, ui.sel, -1);
                else n = relockAchievements(ui.lib, -1, -1);
                ui.toast = std::to_string(n) + (n == 1 ? " achievement" : " achievements") + " re-locked. Press Y to rescan.";
                ui.confirm = 0;
                buildAchView(ui);
                clampScroll(ui);
                dirty = true;
            } else if (down & plat::BTN_B) { ui.confirm = 0; dirty = true; }
        } else if (ui.popup) {
            if (down & (plat::BTN_A | plat::BTN_B)) { ui.popup = false; dirty = true; }
        } else if (ui.debug) {
            if (down) {
                dirty = true;
                int n = (int)ui.lib.games[ui.sel].debug.size();
                if (down & (plat::BTN_B | plat::BTN_X)) ui.debug = false;
                if (down & plat::BTN_UP) ui.debugScroll--;
                if (down & plat::BTN_DOWN) ui.debugScroll++;
                if (down & plat::BTN_L) ui.debugScroll -= DBG_LINES;
                if (down & plat::BTN_R) ui.debugScroll += DBG_LINES;
                ui.debugScroll = std::max(0, std::min(ui.debugScroll, std::max(0, n - DBG_LINES)));
            }
        } else if (down) {
            dirty = true;
            if (down & plat::BTN_RSTICK) openPopupSettings(ui);
            if (down & plat::BTN_Y) {
                ui.loadingLabel = "Rescanning saves";
                ui.lastProgress = -1;
                uint64_t keepId = ui.lib.games[ui.sel].id;
                rescanLibrary(ui.lib, onProgress, &ui);
                ui.icons.clear();   // order may have changed
                ui.heroGi = -1;
                for (size_t i = 0; i < ui.lib.games.size(); i++) if (ui.lib.games[i].id == keepId) ui.sel = (int)i;
                buildOrder(ui);
                buildAchView(ui);
                ui.popup = !ui.lib.newlyUnlocked.empty();
            }
            if (down & plat::BTN_ZR) {
                if (ui.sel != 0) openCaptures(ui);
                else if (ui.screen == SCR_LIBRARY) {
                    ui.loadingLabel = "Checking every save";
                    ui.lastProgress = -1;
                    ScanSummary sum;
                    if (runCompatibilityScan(ui.lib, onProgress, &ui, sum)) {
                        ui.toast = std::to_string(sum.withSave) + " saves checked: " + std::to_string(sum.draftable) + " draftable, " +
                                   std::to_string(sum.text) + " text, " + std::to_string(sum.binary) + " binary. Upload scan_bundle.json.";
                    } else ui.toast = "Scan failed: " + sum.error;
                }
            }
            if (down & plat::BTN_ZL) {
                ui.loadingLabel = "Checking for updates";
                drawLoading(ui, 0, 0);
                ui.updInfo = checkForUpdate();
                ui.upd = 1;
            }
            if (down & plat::BTN_MINUS) {
                const Game& g = ui.lib.games[ui.sel];
                if (ui.screen == SCR_GAME) {
                    if (ui.achView.empty()) ui.toast = "Nothing to re-lock here.";
                    else if (!g.achs[ui.achView[ui.achSel]].unlocked) ui.toast = "This achievement is still locked.";
                    else { ui.relockAch = ui.achView[ui.achSel]; ui.confirm = 1; }
                } else ui.confirm = ui.sel == 0 ? 3 : 2;
            } else if (down & plat::BTN_X) { ui.debug = true; ui.debugScroll = 0; }
            else if (down & plat::BTN_STICK) {
                setLook(ui, !ui.classic);
                ui.screen = SCR_LIBRARY;
                ui.toast = std::string(ui.classic ? "Classic look" : "New look") + " (press a stick to switch back).";
                if (!saveLook(ui)) ui.toast = "Could not write theme.txt (the look resets at the next launch).";
            } else if (ui.classic) {
                bool moved = false;
                if (ui.screen != SCR_GAME) {
                    int before = ui.sel;
                    if (down & plat::BTN_UP) ui.sel--;
                    if (down & plat::BTN_DOWN) ui.sel++;
                    if (down & plat::BTN_L) ui.sel -= LIST_ROWS;
                    if (down & plat::BTN_R) ui.sel += LIST_ROWS;
                    ui.sel = std::max(0, std::min(ui.sel, (int)ui.lib.games.size() - 1));
                    moved = ui.sel != before;
                    if (down & (plat::BTN_A | plat::BTN_RIGHT)) ui.screen = SCR_GAME;
                } else {
                    if (down & plat::BTN_UP) ui.achSel--;
                    if (down & plat::BTN_DOWN) ui.achSel++;
                    if (down & plat::BTN_L) ui.achSel -= CL_ACH_ROWS;
                    if (down & plat::BTN_R) ui.achSel += CL_ACH_ROWS;
                    if (down & (plat::BTN_B | plat::BTN_LEFT)) ui.screen = SCR_LIBRARY;
                }
                if (moved) { ui.achSel = ui.achScroll = 0; buildAchView(ui); }
            } else if (ui.screen == SCR_LIBRARY) {
                int n = (int)ui.order.size(), p = ui.gridPos;
                if ((down & plat::BTN_LEFT) && p > 0) p--;
                if ((down & plat::BTN_RIGHT) && p < n - 1) p++;
                if ((down & plat::BTN_UP) && p >= COLS) p -= COLS;
                if (down & plat::BTN_DOWN) {
                    if (p + COLS < n) p += COLS;
                    else if (p / COLS < (n - 1) / COLS) p = n - 1;   // last row is shorter: go to its last game
                }
                if (p != ui.gridPos) { ui.gridPos = p; ui.sel = ui.order[p]; }
                if (down & (plat::BTN_L | plat::BTN_R)) {
                    ui.sortMode = (ui.sortMode + ((down & plat::BTN_R) ? 1 : SORT_COUNT - 1)) % SORT_COUNT;
                    buildOrder(ui);
                    ui.gridScroll = 0;
                }
                if (down & plat::BTN_A) {
                    ui.screen = SCR_GAME;
                    ui.achFilter = FILT_ALL;
                    ui.achSel = ui.achScroll = 0;
                    buildAchView(ui);
                }
            } else {
                if (down & plat::BTN_UP) ui.achSel--;
                if (down & plat::BTN_DOWN) ui.achSel++;
                if (down & plat::BTN_LEFT) ui.achSel -= ACH_ROWS;
                if (down & plat::BTN_RIGHT) ui.achSel += ACH_ROWS;
                if (down & (plat::BTN_L | plat::BTN_R)) {
                    ui.achFilter = (ui.achFilter + ((down & plat::BTN_R) ? 1 : FILT_COUNT - 1)) % FILT_COUNT;
                    ui.achSel = ui.achScroll = 0;
                    buildAchView(ui);
                }
                if (down & plat::BTN_B) ui.screen = SCR_LIBRARY;
            }
            clampScroll(ui);
        }
        if (dirty) { drawAll(ui); dirty = false; }
        plat::sleepMs(16);
    }
    plat::exit();
    return 0;
}
