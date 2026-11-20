#include "update.h"
#include "cJSON.h"
#include "sha256.h"
#include <cstdio>
#include <cstring>
#include <map>

static const char* DEFAULT_REPO = "Imp3r1us/TrophyNX";
static const char* DEFAULT_BRANCH = "builds";
static const char* MODULE_FILE = "atmosphere/contents/420000000000AC01/exefs.nsp";
static const char* APP_FILE = "switch/TrophyNX.nro";

struct UpdateConfig { std::string token, repo, branch, channel; };

static std::string jstr(cJSON* o, const char* k) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

static std::string trim(std::string s) {
    while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back();
    size_t i = 0;
    while (i < s.size() && (unsigned char)s[i] <= ' ') i++;
    return s.substr(i);
}

static bool loadConfig(UpdateConfig& c, std::string& err, std::string& path) {
    path = plat::dataDir() + "/update.json";
    c.repo = DEFAULT_REPO;
    c.branch = DEFAULT_BRANCH;
    std::vector<uint8_t> buf;
    if (!readFile(path, buf)) return true;   // optional: public repository, no token needed
    buf.push_back(0);
    cJSON* root = cJSON_Parse((const char*)buf.data());
    if (!root) {
        // a file that only holds the token is fine too
        c.token = trim((const char*)buf.data());
        if (c.token.empty() || c.token.find_first_of("{}\" \n") != std::string::npos) { err = "update.json is not valid JSON"; return false; }
    } else {
        c.token = trim(jstr(root, "token"));
        c.repo = trim(jstr(root, "repo"));
        c.branch = trim(jstr(root, "branch"));
        c.channel = trim(jstr(root, "channel"));
        cJSON_Delete(root);
    }
    if (c.repo.empty()) c.repo = DEFAULT_REPO;
    if (c.branch.empty()) c.branch = DEFAULT_BRANCH;
    return true;
}

// accept: raw file of a branch, release JSON, or release asset bytes
static std::vector<std::string> headers(const UpdateConfig& c, const char* accept = "application/vnd.github.raw") {
    std::vector<std::string> h = {std::string("Accept: ") + accept, "X-GitHub-Api-Version: 2022-11-28", "User-Agent: TrophyNX-updater"};
    if (!c.token.empty()) h.push_back("Authorization: Bearer " + c.token);
    return h;
}

static std::string contentsUrl(const UpdateConfig& c, const std::string& path) {
    return "https://api.github.com/repos/" + c.repo + "/contents/" + path + "?ref=" + c.branch;
}

static std::string httpError(int status, const std::string& err, const UpdateConfig& c) {
    if (status == 0) return "No connection to GitHub (is the Switch online?)\n" + err;
    if (status == 401) return "GitHub refused the token in update.json (HTTP 401): it is wrong or expired.";
    if (status == 403 || status == 429) return "GitHub refused the request (HTTP " + std::to_string(status) + "): too many checks in the last hour\n(60 per hour without a token), try again later.";
    if (status == 404) return "Not found (HTTP 404): " + c.repo + " has no " + (c.channel == "builds" ? "\"" + c.branch + "\" branch with version.json" : std::string("release")) +
                              " yet\n(it appears after the next CI build)" + (c.token.empty() ? "." : ", or the token can't read it.");
    return "GitHub answered HTTP " + std::to_string(status) + (err.empty() ? "" : ": " + err);
}

// Installed location of a build file: the running .nro for the app, else the same path on the SD.
static std::string localPath(const std::string& path) {
    if (path == APP_FILE) {
        std::string self = plat::selfPath();
        if (self.size() > 4 && self.compare(self.size() - 4, 4, ".nro") == 0) return self;
    }
    return plat::sdRoot() + path;
}

static bool safePath(const std::string& p) {
    if (p.empty() || p[0] == '/' || p.find('\\') != std::string::npos || p.find(':') != std::string::npos) return false;
    if (p.find("..") != std::string::npos) return false;
    return p.rfind("switch/", 0) == 0 || p.rfind("atmosphere/contents/420000000000AC01/", 0) == 0;
}

// Latest release: its version.json asset, and the API URL of every asset by name.
static bool fetchRelease(const UpdateConfig& cfg, UpdateInfo& info, std::vector<uint8_t>& manifest, std::map<std::string, std::string>& assets,
                         int& status, std::string& err) {
    std::vector<uint8_t> body;
    status = plat::httpGet("https://api.github.com/repos/" + cfg.repo + "/releases/latest", headers(cfg, "application/vnd.github+json"), body, nullptr, nullptr, err);
    if (status != 200) return false;
    body.push_back(0);
    cJSON* root = cJSON_Parse((const char*)body.data());
    if (!root) { err = "invalid answer from GitHub (release)"; status = -1; return false; }
    info.source = "release " + jstr(root, "tag_name");
    cJSON* list = cJSON_GetObjectItemCaseSensitive(root, "assets");
    cJSON* a;
    if (cJSON_IsArray(list)) cJSON_ArrayForEach(a, list) {
        std::string name = jstr(a, "name"), url = jstr(a, "url");
        if (!name.empty() && url.rfind("https://api.github.com/", 0) == 0) assets[name] = url;
    }
    cJSON_Delete(root);
    auto it = assets.find("version.json");
    if (it == assets.end()) { err = "the latest release has no version.json"; status = -1; return false; }
    status = plat::httpGet(it->second, headers(cfg, "application/octet-stream"), manifest, nullptr, nullptr, err);
    return status == 200;
}

UpdateInfo checkForUpdate() {
    UpdateInfo info;
    UpdateConfig cfg;
    std::string err;
    if (!loadConfig(cfg, err, info.configPath)) { info.error = err; return info; }
    std::vector<uint8_t> body;
    std::map<std::string, std::string> assets;
    int st = 0;
    if (cfg.channel != "builds") {
        info.fromRelease = true;
        if (!fetchRelease(cfg, info, body, assets, st, err)) {
            info.error = st == -1 ? err : httpError(st, err, cfg);
            return info;
        }
    } else {
        info.source = cfg.branch + " branch";
        st = plat::httpGet(contentsUrl(cfg, "version.json"), headers(cfg), body, nullptr, nullptr, err);
        if (st != 200) { info.error = httpError(st, err, cfg); return info; }
    }
    body.push_back(0);
    cJSON* root = cJSON_Parse((const char*)body.data());
    if (!root) { info.error = "version.json (" + info.source + ") is not valid JSON"; return info; }
    info.commit = jstr(root, "commit");
    info.date = jstr(root, "date");
    info.message = jstr(root, "message");
    info.appVersion = jstr(root, "app_version");
    info.moduleVersion = jstr(root, "module_version");
    cJSON* files = cJSON_GetObjectItemCaseSensitive(root, "files");
    cJSON* f;
    std::string missing;
    if (cJSON_IsArray(files)) cJSON_ArrayForEach(f, files) {
        UpdateFile u;
        u.path = jstr(f, "path");
        u.sha256 = jstr(f, "sha256");
        cJSON* sz = cJSON_GetObjectItemCaseSensitive(f, "size");
        u.size = cJSON_IsNumber(sz) ? (uint64_t)sz->valuedouble : 0;
        if (!safePath(u.path) || u.sha256.size() != 64) continue;   // never write outside our folders
        if (info.fromRelease) {
            std::string asset = jstr(f, "asset");
            auto it = assets.find(asset);
            if (it != assets.end()) u.url = it->second;
            else if (u.size > 0) missing += (missing.empty() ? "" : ", ") + u.path;   // empty files have no asset
        } else u.url = contentsUrl(cfg, "sd/" + u.path);
        u.local = localPath(u.path);
        uint64_t have = 0;
        u.changed = sha256File(u.local, &have) != u.sha256 || have != u.size;
        if (u.changed) {
            info.downloadBytes += u.size;
            if (u.path == APP_FILE) info.appChanged = true;
            if (u.path == MODULE_FILE) info.moduleChanged = true;
        }
        info.files.push_back(u);
    }
    cJSON_Delete(root);
    if (!missing.empty()) { info.error = "The " + info.source + " is incomplete (no asset for " + missing + ")."; return info; }
    if (info.files.empty()) { info.error = "version.json lists no files"; return info; }
    bool any = false;
    for (const UpdateFile& u : info.files) any |= u.changed;
    info.state = any ? UpdateInfo::Available : UpdateInfo::UpToDate;
    return info;
}

static std::string dirOf(const std::string& p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? "" : p.substr(0, s);
}

struct DlProgress {
    plat::ProgressFn fn;
    void* ctx;
    uint64_t doneKb, totalKb;
};
static void dlProgress(int done, int, void* c) {
    DlProgress* p = (DlProgress*)c;
    if (p->fn) p->fn((int)(p->doneKb + done), (int)p->totalKb, p->ctx);
}

bool applyUpdate(const UpdateInfo& info, plat::ProgressFn progress, void* ctx, std::string& summary) {
    UpdateConfig cfg;
    std::string err, cfgPath;
    if (!loadConfig(cfg, err, cfgPath)) { summary = "update.json: " + err; return false; }
    DlProgress dp = {progress, ctx, 0, (info.downloadBytes >> 10) + 1};
    std::vector<const UpdateFile*> todo;
    for (const UpdateFile& u : info.files) if (u.changed) todo.push_back(&u);
    // 1. download everything next to its destination
    for (const UpdateFile* u : todo) {
        std::vector<uint8_t> body;
        int st = 200;
        if (u->size > 0)   // empty files (boot2.flag) are not downloaded
            st = plat::httpGet(u->url, info.fromRelease ? headers(cfg, "application/octet-stream") : headers(cfg), body, dlProgress, &dp, err);
        bool ok = st == 200 && body.size() == u->size && sha256Hex(body.data(), body.size()) == u->sha256;
        if (ok) {
            makeDirs(dirOf(u->local));
            std::string data((const char*)body.data(), body.size());
            ok = writeFile(u->local + ".new", data);
            if (!ok) err = "Could not write " + u->local + ".new\n(SD card full or read-only?)";
        } else if (st == 200) err = "The download of " + u->path + " was corrupted\n(size or SHA-256 differs from version.json): try again.";
        else err = httpError(st, err, cfg);
        if (!ok) {
            for (const UpdateFile* v : todo) remove((v->local + ".new").c_str());
            summary = "Nothing was changed.\n" + err;
            return false;
        }
        dp.doneKb += u->size >> 10;
    }
    // 2. swap them in. The running .nro stays open while romfs is mounted (and an open file can't be
    //    deleted on the Switch): release it first. The app has to restart afterwards anyway.
    if (info.appChanged) plat::releaseSelfFile();
    std::string failed, log;
    for (const UpdateFile* u : todo) {
        std::string tmp = u->local + ".new", old = u->local + ".old";
        remove(old.c_str());
        bool had = pathExists(u->local);
        if (had && rename(u->local.c_str(), old.c_str()) != 0 && remove(u->local.c_str()) != 0) {
            failed += (failed.empty() ? "" : ", ") + u->path;
            log += "  " + u->local + ": in use, could not be replaced\n";
            continue;
        }
        if (rename(tmp.c_str(), u->local.c_str()) != 0) {
            if (had) rename(old.c_str(), u->local.c_str());   // put the old one back
            failed += (failed.empty() ? "" : ", ") + u->path;
            log += "  " + u->local + ": rename failed\n";
            continue;
        }
        remove(old.c_str());
        uint64_t have = 0;
        bool good = sha256File(u->local, &have) == u->sha256 && have == u->size;
        log += "  " + u->local + (good ? ": updated\n" : ": CHECK FAILED after the swap\n");
        if (!good) failed += (failed.empty() ? "" : ", ") + u->path;
    }
    // the live module is restarted even if another file failed: its new file may already be in place
    std::string modMsg;
    if (info.moduleChanged && failed.find("exefs.nsp") == std::string::npos) {
        plat::restartLiveModule(modMsg);
        log += "  live module: " + modMsg + "\n";
    }
    writeFile(plat::dataDir() + "/update_log.txt", "Update from " + info.source + " (app " + info.appVersion + ", module " + info.moduleVersion + ")\n" + log);
    if (!failed.empty()) {
        summary = "Could not replace " + failed + "\n(a new version left as .new is installed by the live module at the next reboot)." +
                  (modMsg.empty() ? "" : "\n" + modMsg);
        return false;
    }
    summary = std::to_string(todo.size()) + (todo.size() == 1 ? " file" : " files") + " updated";
    if (!modMsg.empty()) summary += "; " + modMsg;
    summary += ".";
    return true;
}
