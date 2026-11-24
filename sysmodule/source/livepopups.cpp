// Live save "following" popups, see include/livepopups.h.
#include "livepopups.h"
#include "overlay_data.h"
#include "popup_draw.h"
#include <algorithm>
#include <cstdio>

static bool isMet(const Achievement& a) { return a.value + 1e-9 >= a.rule.target; }

static PopupRequest requestFor(const Game& g, const Achievement& a, int kind) {
    PopupRequest pr;
    pr.titleId = g.id;
    pr.achId = a.id;
    pr.title = a.name;
    pr.desc = a.desc;
    pr.game = g.name;
    pr.points = a.points;
    pr.tier = popupTierFor(a.tier.empty() ? nullptr : a.tier.c_str(), a.points);
    pr.hidden = a.hidden;
    pr.kind = kind;
    return pr;
}

static std::string progressLine(const Achievement& a) {   // same text as the app and the overlay
    ov::Ach o;
    o.value = a.value;
    o.target = a.rule.target;
    o.divisor = a.rule.divisor;
    return ov::progressText(o);
}

std::vector<PopupRequest> livePopups(LiveFollow& st, const Game& g, const std::set<std::string>& unlockedBefore,
                                     const PopupOptions& opts, unsigned long long readMs) {
    std::vector<PopupRequest> out;
    std::vector<const Achievement*> save;
    for (const Achievement& a : g.achs) if (isSaveRule(a.rule.type)) save.push_back(&a);

    if (!st.checked) {
        st.checked = true;
        if (opts.liveCheck) {
            int met = 0;
            for (const Achievement* a : save) met += isMet(*a) ? 1 : 0;
            PopupRequest pr;
            pr.titleId = g.id;
            pr.achId = "_live_check";
            pr.game = g.name;
            pr.kind = POPUP_LIVE_CHECK;
            char line[96];
            snprintf(line, sizeof(line), "%d/%d met \xC2\xB7 read in %llu ms", met, (int)save.size(), readMs);
            pr.line = line;
            out.push_back(pr);
            // proof it reads the save: achievements already unlocked that this save meets, highest points first
            std::vector<const Achievement*> known;
            for (const Achievement* a : save) if (isMet(*a) && unlockedBefore.count(a->id)) known.push_back(a);
            std::stable_sort(known.begin(), known.end(), [](const Achievement* x, const Achievement* y) { return x->points > y->points; });
            for (size_t i = 0; i < known.size() && i < 3; i++) {
                PopupRequest k = requestFor(g, *known[i], POPUP_KNOWN);
                k.line = "met in your save";
                out.push_back(k);
            }
        }
    } else if (opts.progress) {
        std::vector<const Achievement*> up;
        for (const Achievement* a : save) {
            auto it = st.last.find(a->id);
            if (it == st.last.end() || isMet(*a) || a->hidden || !a->rule.showProgress || a->rule.target <= 1) continue;
            if (a->value > it->second + 1e-9) up.push_back(a);
        }
        std::stable_sort(up.begin(), up.end(), [](const Achievement* x, const Achievement* y) {
            return x->value / x->rule.target > y->value / y->rule.target;   // closest to done first
        });
        for (size_t i = 0; i < up.size() && i < 3; i++) {
            PopupRequest p = requestFor(g, *up[i], POPUP_PROGRESS);
            p.line = progressLine(*up[i]);
            out.push_back(p);
        }
        if (up.size() > 3) {
            PopupRequest more;
            more.titleId = g.id;
            more.achId = "_progress_more";
            more.game = g.name;
            more.kind = POPUP_PROGRESS;
            more.title = "+" + std::to_string(up.size() - 3) + " more achievements";
            more.line = "progressed";
            out.push_back(more);
        }
    }
    for (const Achievement* a : save) st.last[a->id] = a->value;
    return out;
}
