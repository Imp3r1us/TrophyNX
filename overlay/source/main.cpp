// TrophyNX Tesla overlay: the running game's achievements, without leaving it.
//
// Reads, never writes (see include/overlay_data.h):
//   <data>/overlay/<TID>.json       the game's list, written by the app after each scan
//   <data>/overlay/<TID>.live.json  progress written by the live module while you play
//   <data>/overlay/<TID>.memtest.json  live memory test (version, coverage, values), while the module watches memory
//   <data>/unlocks/<user>.json      unlocked achievements and when
// Built with libtesla (GPL-2.0, like TrophyNX). Open Tesla (L + Dpad Down + Right stick by default), pick TrophyNX.
#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include "overlay_data.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <sys/stat.h>

namespace {

bool g_acc = false, g_pminfo = false, g_sd = false;
bool g_dirty = false;   // shown again: the main list reloads its data

// ---------------------------------------------------------------- data
std::string dataDir() {   // same as the live module (sysmodule/source/store.cpp)
    struct stat st;
    if (stat("sdmc:/switch/TrophyNX", &st) == 0) return "sdmc:/switch/TrophyNX";
    if (stat("sdmc:/switch/SwitchAchievements", &st) == 0) return "sdmc:/switch/SwitchAchievements";
    return "sdmc:/switch/TrophyNX";
}

// libtesla's own SD helper mounts and unmounts "sdmc" around its settings reads: check before each load.
bool ensureSd() {
    struct stat st;
    if (stat("sdmc:/", &st) == 0) return true;
    g_sd = R_SUCCEEDED(fsdevMountSdmc());
    return g_sd;
}

bool readText(const std::string& path, std::string& out, long maxSize = 4 * 1024 * 1024) {
    out.clear();
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = sz >= 0 && sz <= maxSize;
    if (ok && sz > 0) {
        out.resize((size_t)sz);
        ok = fread(&out[0], 1, (size_t)sz, f) == (size_t)sz;
    }
    fclose(f);
    if (!ok) out.clear();
    return ok;
}

std::string userKey() {   // same key as the app and the live module
    AccountUid uid = {};
    if (!g_acc || R_FAILED(accountGetLastOpenedUser(&uid)) || !accountUidIsValid(&uid)) return "";
    char b[40];
    snprintf(b, sizeof(b), "%016lx%016lx", (unsigned long)uid.uid[1], (unsigned long)uid.uid[0]);
    return b;
}

uint64_t runningTitle() {
    u64 pid = 0, tid = 0;
    if (!g_pminfo || R_FAILED(pmdmntGetApplicationProcessId(&pid)) || !pid) return 0;
    if (R_FAILED(pminfoGetProgramId(&tid, pid))) return 0;
    return tid;
}

struct Data {
    enum State { NoGame, NoList, Ok } state = NoGame;
    ov::GameData game;
    ov::Sections sec;
    bool hasMemTest = false;
    ov::MemTest mem;
    std::string signature;   // the files it was built from (refreshed when they change)
};

// Size + modification time of the files a list is built from: cheap to check while the overlay is open.
std::string filesSignature(uint64_t tid) {
    std::string dir = dataDir(), sig, user = userKey();
    std::string paths[] = {ov::livePath(dir, tid), ov::memTestPath(dir, tid), ov::snapshotPath(dir, tid),
                           user.empty() ? std::string() : dir + "/unlocks/" + user + ".json"};
    for (const std::string& p : paths) {
        struct stat st;
        if (p.empty() || stat(p.c_str(), &st) != 0) { sig += "-;"; continue; }
        sig += std::to_string((long long)st.st_size) + "@" + std::to_string((long long)st.st_mtime) + ";";
    }
    return std::to_string(tid) + ":" + sig;
}

std::shared_ptr<Data> loadData() {
    auto d = std::make_shared<Data>();
    if (!ensureSd()) return d;
    uint64_t tid = runningTitle();
    if (!tid) return d;
    d->game.titleId = tid;
    d->signature = filesSignature(tid);
    std::string dir = dataDir(), text;
    if (readText(ov::memTestPath(dir, tid), text, 256 * 1024) && ov::parseMemTest(text.c_str(), d->mem) && d->mem.titleId == tid)
        d->hasMemTest = true;
    if (!readText(ov::snapshotPath(dir, tid), text) || !ov::parseSnapshot(text.c_str(), d->game) || d->game.achs.empty()) {
        d->game.titleId = tid;
        d->state = Data::NoList;
        return d;
    }
    std::map<std::string, uint64_t> unlocks;
    std::string user = userKey();
    if (!user.empty() && readText(dir + "/unlocks/" + user + ".json", text)) unlocks = ov::storeEntriesFor(text, tid);
    std::map<std::string, double> live;
    if (readText(ov::livePath(dir, tid), text, 256 * 1024)) live = ov::parseLive(text.c_str());
    ov::merge(d->game, unlocks, live);
    d->sec = ov::sections(d->game);
    d->state = Data::Ok;
    return d;
}

// ---------------------------------------------------------------- drawing helpers
using tsl::Color;   // libtesla declares Color in tsl, not tsl::gfx
using tsl::gfx::Renderer;

const Color C_TEXT = tsl::style::color::ColorText;
const Color C_DIM = tsl::style::color::ColorDescription;
const Color C_TRACK = {0x3, 0x3, 0x3, 0xF};
const Color C_GREEN = {0x3, 0xD, 0x6, 0xF};

Color tierColor(int tier) {
    switch (tier) {
        case ov::TIER_PLATINUM: return {0x9, 0xD, 0xF, 0xF};
        case ov::TIER_GOLD: return {0xF, 0xC, 0x2, 0xF};
        case ov::TIER_SILVER: return {0xB, 0xB, 0xC, 0xF};
        default: return {0xC, 0x7, 0x3, 0xF};
    }
}

int textWidth(Renderer* r, const std::string& s, float size) {
    return (int)r->drawString(s.c_str(), false, 0, 0, size, tsl::style::color::ColorTransparent).first;
}

void bar(Renderer* r, s32 x, s32 y, s32 w, s32 h, double ratio, Color fill) {
    r->drawRect(x, y, w, h, r->a(C_TRACK));
    s32 fw = (s32)(w * (ratio < 0 ? 0 : ratio > 1 ? 1 : ratio));
    if (fw > 0) r->drawRect(x, y, fw, h, r->a(fill));
}

void medal(Renderer* r, s32 cx, s32 cy, u16 radius, int tier, bool earned) {
    Color c = earned ? tierColor(tier) : C_TRACK;
    r->drawCircle(cx, cy, radius, true, r->a(c));
    Color inner = earned ? Color{(u8)(c.r * 3 / 4), (u8)(c.g * 3 / 4), (u8)(c.b * 3 / 4), 0xF} : Color{0x2, 0x2, 0x2, 0xF};
    r->drawCircle(cx, cy, (u16)(radius * 3 / 5), true, r->a(inner));
}

// Wrapped text, measured once (text measuring goes through the font renderer); returns the height used.
struct Wrapped {
    std::string text;
    s32 width = -1;
    std::vector<std::string> lines;
};
s32 paragraph(Renderer* r, Wrapped& cache, const std::string& text, s32 x, s32 y, s32 w, float size, Color c, size_t maxLines = 8) {
    if (cache.width != w || cache.text != text) {
        cache.lines = ov::wrap(text, w, [&](const std::string& s) { return textWidth(r, s, size); }, maxLines);
        cache.text = text;
        cache.width = w;
    }
    const std::vector<std::string>& lines = cache.lines;
    s32 lh = (s32)(size * 1.35f);
    for (size_t i = 0; i < lines.size(); i++) r->drawString(lines[i].c_str(), false, x, y + (s32)size + lh * (s32)i, size, r->a(c));
    return lh * (s32)lines.size();
}

// Rough line count before a renderer exists (list item heights are fixed when the page is built).
int estimateLines(const std::string& text, int width, float size) {
    int perLine = std::max(1, (int)(width / (size * 0.52f)));
    int n = 0, len = 0;
    for (char c : text) if ((c & 0xC0) != 0x80) len++;   // UTF-8 characters
    n = (len + perLine - 1) / perLine;
    return std::min(8, std::max(1, n + 1));
}

// ---------------------------------------------------------------- detail page
class AchievementGui : public tsl::Gui {
public:
    AchievementGui(std::shared_ptr<Data> data, size_t index) : m_data(std::move(data)), m_index(index) {}

    tsl::elm::Element* createUI() override {
        const ov::Ach& a = m_data->game.achs[m_index];
        auto frame = new tsl::elm::OverlayFrame("TrophyNX", m_data->game.name);
        auto list = new tsl::elm::List();
        std::string desc = ov::descText(a);
        int h = 120 + estimateLines(a.name, 300, 24) * 32 + estimateLines(desc, 380, 19) * 26 + 90;
        auto data = m_data;
        size_t idx = m_index;
        auto wrapName = std::make_shared<Wrapped>(), wrapDesc = std::make_shared<Wrapped>();
        list->addItem(new tsl::elm::CustomDrawer([data, idx, wrapName, wrapDesc](Renderer* r, s32 x, s32 y, s32 w, s32) {
            const ov::Ach& a = data->game.achs[idx];
            s32 cy = y + 20;
            // medal, name, tier and points
            medal(r, x + 34, cy + 30, 26, a.tier, a.unlocked);
            s32 tx = x + 78;
            s32 used = paragraph(r, *wrapName, a.name, tx, cy, w - 78, 24, a.unlocked ? C_TEXT : C_DIM, 3);
            std::string meta = std::string(ov::tierName(a.tier)) + "  -  " + std::to_string(a.points) + " points";
            meta[0] = (char)toupper((unsigned char)meta[0]);
            r->drawString(meta.c_str(), false, tx, cy + used + 22, 17, r->a(tierColor(a.tier)));
            cy += std::max<s32>(70, used + 34) + 20;
            // description
            cy += paragraph(r, *wrapDesc, ov::descText(a), x + 8, cy, w - 16, 19, a.hidden && !a.unlocked ? C_DIM : C_TEXT) + 22;
            // status
            if (a.unlocked) {
                std::string t = "Unlocked " + ov::dateText(a.unlockTime);
                r->drawCircle(x + 18, cy + 12, 10, true, r->a(C_GREEN));
                r->drawString(t.c_str(), false, x + 38, cy + 19, 19, r->a(C_GREEN));
            } else if (a.showProgress && !a.hidden) {
                std::string p = ov::progressText(a);
                r->drawString("Progress", false, x + 8, cy + 19, 18, r->a(C_DIM));
                r->drawString(p.c_str(), false, x + w - 8 - textWidth(r, p, 19), cy + 19, 19, r->a(C_TEXT));
                bar(r, x + 8, cy + 32, w - 16, 8, ov::progressRatio(a), tierColor(a.tier));
            } else {
                r->drawString("Locked", false, x + 8, cy + 19, 19, r->a(C_DIM));
            }
        }), (u16)std::min(h, 540));   // the page has nothing to focus, so it never scrolls: stay within the frame
        frame->setContent(list);
        return frame;
    }

private:
    std::shared_ptr<Data> m_data;
    size_t m_index;
};

// ---------------------------------------------------------------- main list
class MainGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        m_frame = new tsl::elm::OverlayFrame("TrophyNX", "");
        m_list = new tsl::elm::List();
        fill();
        m_frame->setContent(m_list);
        return m_frame;
    }

    // Reload when shown again. List::clear() only flags the list: its items are deleted at the next draw,
    // so the new ones are added on the frame after (adding them now would delete them too).
    void update() override {
        if (m_refill) {
            m_refill = false;
            fill();
            requestFocus(m_list, tsl::FocusDirection::None, false);
            return;
        }
        // While open: rebuild when the live module wrote new progress or unlocked something (checked every ~2 s)
        if (!g_dirty && m_data && ++m_frames >= 120) {
            m_frames = 0;
            uint64_t tid = runningTitle();
            if (ensureSd() && (tid != m_data->game.titleId || (tid && filesSignature(tid) != m_data->signature))) g_dirty = true;
        }
        if (!g_dirty) return;
        g_dirty = false;
        removeFocus();   // libtesla: nothing in a list may be focused when it is cleared
        m_list->clear();
        m_refill = true;
    }

private:
    tsl::elm::OverlayFrame* m_frame = nullptr;
    tsl::elm::List* m_list = nullptr;
    std::shared_ptr<Data> m_data;
    bool m_refill = false;
    int m_frames = 0;

    void message(const std::string& text) {
        auto cache = std::make_shared<Wrapped>();
        m_list->addItem(new tsl::elm::CustomDrawer([text, cache](Renderer* r, s32 x, s32 y, s32 w, s32) {
            paragraph(r, *cache, text, x + 10, y + 30, w - 20, 19, C_DIM);
        }), 220);
    }

    void fill() {
        m_data = loadData();
        const Data& d = *m_data;
        if (d.state == Data::NoGame) {
            m_frame->setSubtitle("No game running");
            message(ensureSd() ? "Start a game, then open TrophyNX here to see its achievements and your progress."
                         : "The SD card could not be opened.");
            return;
        }
        if (d.state == Data::NoList) {
            m_frame->setSubtitle(ov::titleHex(d.game.titleId));
            if (d.hasMemTest) addMemTest();
            message("No list for this game yet. Open the TrophyNX app once (it prepares every game's list for this "
                    "menu when it scans), then come back.");
            return;
        }
        m_frame->setSubtitle(d.game.name);
        auto data = m_data;
        // summary: unlocked, points, medals
        m_list->addItem(new tsl::elm::CustomDrawer([data](Renderer* r, s32 x, s32 y, s32 w, s32) {
            const ov::GameData& g = data->game;
            std::string count = std::to_string(g.unlocked) + " / " + std::to_string(g.achs.size()) + " unlocked";
            int pct = g.achs.empty() ? 0 : (int)(100.0 * g.unlocked / g.achs.size() + 0.5);
            std::string ps = std::to_string(pct) + "%";
            r->drawString(count.c_str(), false, x + 8, y + 34, 22, r->a(C_TEXT));
            r->drawString(ps.c_str(), false, x + w - 8 - textWidth(r, ps, 22), y + 34, 22, r->a(C_GREEN));
            bar(r, x + 8, y + 46, w - 16, 8, g.achs.empty() ? 0 : (double)g.unlocked / g.achs.size(), C_GREEN);
            std::string pts = std::to_string(g.points) + " / " + std::to_string(g.maxPoints) + " points";
            r->drawString(pts.c_str(), false, x + 8, y + 84, 18, r->a(C_DIM));
            // medals earned per tier, right-aligned
            int counts[4] = {0, 0, 0, 0};
            for (const ov::Ach& a : g.achs) if (a.unlocked) counts[a.tier < 0 ? 0 : a.tier > 3 ? 3 : a.tier]++;
            s32 cx = x + w - 8;
            for (int t = 3; t >= 0; t--) {
                if (t == ov::TIER_PLATINUM && !counts[t]) continue;
                std::string n = std::to_string(counts[t]);
                cx -= textWidth(r, n, 18);
                r->drawString(n.c_str(), false, cx, y + 84, 18, r->a(C_TEXT));
                cx -= 16;
                medal(r, cx, y + 78, 9, t, true);
                cx -= 20;
            }
        }), 104);
        if (d.hasMemTest) addMemTest();
        addSection("In progress", d.sec.inProgress);
        addSection("Locked", d.sec.locked);
        addSection("Unlocked", d.sec.unlocked);
    }

    // Live memory test: the running version, how many memory achievements it covers, and each value read.
    void addMemTest() {
        auto data = m_data;
        const ov::MemTest& t = data->mem;
        m_list->addItem(new tsl::elm::CategoryHeader("Live memory test"));
        size_t rows = std::min<size_t>(t.items.size(), 12);
        m_list->addItem(new tsl::elm::CustomDrawer([data, rows](Renderer* r, s32 x, s32 y, s32 w, s32) {
            const ov::MemTest& t = data->mem;
            std::string ver = "Version " + (t.build.empty() ? std::string("?") : t.build);
            r->drawString(ver.c_str(), false, x + 8, y + 26, 18, r->a(C_TEXT));
            std::string state = t.reading ? "Watching " + std::to_string(t.watched) + " of " + std::to_string(t.total) + " achievements"
                                          : (t.note.empty() ? std::string("Not watching") : t.note) + ": add this build to the definition";
            r->drawString(state.c_str(), false, x + 8, y + 52, 16, r->a(t.reading ? C_GREEN : tierColor(ov::TIER_GOLD)));
            s32 cy = y + 84;
            for (size_t k = 0; k < rows; k++, cy += 28) {
                const ov::MemTestItem& i = t.items[k];
                std::string name = i.id;
                for (const ov::Ach& a : data->game.achs) if (a.id == i.id) name = a.name;
                std::string v;
                Color c = C_DIM;
                if (i.done) { v = "Unlocked"; c = C_GREEN; }
                else if (!i.read) v = t.reading ? "no value" : "-";
                else {
                    v = ov::valueText(i.value) + "  " + i.cmp + " " + ov::valueText(i.target);
                    c = i.holds ? C_GREEN : C_TEXT;
                }
                s32 vw = textWidth(r, v, 16);
                std::string shown = name;
                while (shown.size() > 4 && textWidth(r, shown, 16) > w - 24 - vw) shown = shown.substr(0, shown.size() - 4) + "...";
                r->drawString(shown.c_str(), false, x + 8, cy, 16, r->a(C_TEXT));
                r->drawString(v.c_str(), false, x + w - 8 - vw, cy, 16, r->a(c));
            }
        }), (u16)(96 + 28 * rows));
    }

    void addSection(const char* title, const std::vector<size_t>& idx) {
        if (idx.empty()) return;
        m_list->addItem(new tsl::elm::CategoryHeader(std::string(title) + " (" + std::to_string(idx.size()) + ")"));
        auto data = m_data;
        for (size_t i : idx) {
            const ov::Ach& a = data->game.achs[i];
            auto item = new tsl::elm::ListItem(a.name);
            if (a.unlocked) item->setValue(ov::dateText(a.unlockTime), true);
            else if (a.showProgress && !a.hidden && a.value > 0) item->setValue(ov::progressText(a));
            else item->setValue(std::to_string(a.points) + " pts", true);
            item->setClickListener([data, i](u64 keys) {
                if (keys & HidNpadButton_A) {
                    tsl::changeTo<AchievementGui>(data, i);
                    return true;
                }
                return false;
            });
            m_list->addItem(item);
        }
    }
};

// ---------------------------------------------------------------- overlay
class TrophyOverlay : public tsl::Overlay {
public:
    void initServices() override {
        g_sd = R_SUCCEEDED(fsdevMountSdmc());   // libtesla opens fs but doesn't mount the SD card
        g_pminfo = R_SUCCEEDED(pminfoInitialize());
        g_acc = R_SUCCEEDED(accountInitialize(AccountServiceType_Administrator)) ||
                R_SUCCEEDED(accountInitialize(AccountServiceType_Application));
    }

    void exitServices() override {
        if (g_acc) accountExit();
        if (g_pminfo) pminfoExit();
        if (g_sd) fsdevUnmountDevice("sdmc");
    }

    void onShow() override {
        if (m_shown) g_dirty = true;   // the first show uses the list just built
        m_shown = true;
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<MainGui>(); }

private:
    bool m_shown = false;
};

}  // namespace

int main(int argc, char** argv) { return tsl::loop<TrophyOverlay>(argc, argv); }
