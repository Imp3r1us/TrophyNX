#include "captures.h"
#include "platform.h"
#include "cJSON.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

// ---------------------------------------------------------------- helpers
static uint32_t crcTable(int n) {
    static uint32_t t[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        init = true;
    }
    return t[n];
}
static uint32_t crcUpdate(uint32_t crc, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) crc = (crc >> 8) ^ crcTable((crc ^ p[i]) & 0xFF);
    return crc;
}

static std::string hex16(uint64_t v) {
    char b[24];
    snprintf(b, sizeof(b), "%016llX", (unsigned long long)v);
    return b;
}
static bool isHex16(const std::string& s) {
    if (s.size() != 16) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
    return true;
}
static bool isStamp(const std::string& s) {   // YYYYMMDD-HHMMSS, optionally -N
    if (s.size() < 15 || s.size() > 20 || s[8] != '-') return false;
    for (size_t i = 0; i < s.size(); i++) {
        if (i == 8 || (i == 15 && s[i] == '-')) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}
static bool isDir(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
static void removeTree(const std::string& p, int depth = 0) {
    if (depth > 12) return;
    if (isDir(p)) {
        for (const std::string& n : listDir(p)) removeTree(p + "/" + n, depth + 1);
        rmdir(p.c_str());
    } else remove(p.c_str());
}
static std::string withSlash(const std::string& s) { return (!s.empty() && s.back() == '/') ? s : s + "/"; }

std::string capturesDir(const std::string& dataDir) { return dataDir + "/captures"; }

// ---------------------------------------------------------------- settings
CaptureSettings loadCaptureSettings(const std::string& dataDir) {
    CaptureSettings s;
    std::vector<uint8_t> buf;
    if (!readFile(capturesDir(dataDir) + "/settings.json", buf)) return s;
    buf.push_back(0);
    cJSON* root = cJSON_Parse((const char*)buf.data());
    if (!root) return s;
    cJSON* e;
    cJSON_ArrayForEach(e, cJSON_GetObjectItem(root, "games")) {
        if (!cJSON_IsString(e)) continue;
        uint64_t id = strtoull(e->valuestring, nullptr, 16);
        if (id) s.games.push_back(id);
    }
    cJSON* k = cJSON_GetObjectItem(root, "keep");
    if (cJSON_IsNumber(k)) s.keep = std::max(1, std::min(CAPTURE_KEEP_MAX, k->valueint));
    cJSON_Delete(root);
    return s;
}

bool saveCaptureSettings(const std::string& dataDir, const CaptureSettings& s) {
    cJSON* root = cJSON_CreateObject();
    cJSON* g = cJSON_AddArrayToObject(root, "games");
    for (uint64_t id : s.games) cJSON_AddItemToArray(g, cJSON_CreateString(hex16(id).c_str()));
    cJSON_AddNumberToObject(root, "keep", s.keep);
    char* txt = cJSON_Print(root);
    cJSON_Delete(root);
    if (!txt) return false;
    makeDirs(capturesDir(dataDir));
    bool ok = writeFile(capturesDir(dataDir) + "/settings.json", std::string(txt) + "\n");
    free(txt);
    return ok;
}

bool captureEnabled(const CaptureSettings& s, uint64_t id) {
    return std::find(s.games.begin(), s.games.end(), id) != s.games.end();
}
void setCaptureEnabled(CaptureSettings& s, uint64_t id, bool on) {
    auto it = std::find(s.games.begin(), s.games.end(), id);
    if (on && it == s.games.end()) s.games.push_back(id);
    if (!on && it != s.games.end()) s.games.erase(it);
}

// ---------------------------------------------------------------- capture
struct CapFile { std::string path; uint64_t size; uint32_t crc; };

static bool readInfo(const std::string& jsonPath, CaptureInfo* info, std::vector<CapFile>* files, std::string* name) {
    std::vector<uint8_t> buf;
    if (!readFile(jsonPath, buf)) return false;
    buf.push_back(0);
    cJSON* root = cJSON_Parse((const char*)buf.data());
    if (!root) return false;
    cJSON* v;
    if (info) {
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(root, "time"))) info->time = (uint64_t)v->valuedouble;
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(root, "session_minutes"))) info->sessionMinutes = v->valueint;
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(root, "bytes"))) info->bytes = (uint64_t)v->valuedouble;
        info->files = cJSON_GetArraySize(cJSON_GetObjectItem(root, "files"));
    }
    if (name && cJSON_IsString(v = cJSON_GetObjectItem(root, "name"))) *name = v->valuestring;
    if (files) {
        cJSON* f;
        cJSON_ArrayForEach(f, cJSON_GetObjectItem(root, "files")) {
            cJSON* p = cJSON_GetObjectItem(f, "path");
            cJSON* s = cJSON_GetObjectItem(f, "size");
            cJSON* c = cJSON_GetObjectItem(f, "crc32");
            if (cJSON_IsString(p) && cJSON_IsNumber(s) && cJSON_IsString(c))
                files->push_back({p->valuestring, (uint64_t)s->valuedouble, (uint32_t)strtoul(c->valuestring, nullptr, 16)});
        }
    }
    cJSON_Delete(root);
    return true;
}

// Streamed copy (small fixed buffer: the module has a 4 MB heap) computing the CRC32 on the way.
static bool copyFile(const std::string& from, const std::string& to, uint64_t& size, uint32_t& crc) {
    static uint8_t buf[64 * 1024];
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(to.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    bool ok = true;
    size = 0;
    uint32_t c = 0xFFFFFFFFu;
    for (;;) {
        size_t n = fread(buf, 1, sizeof(buf), in);
        if (n == 0) { ok = !ferror(in); break; }
        c = crcUpdate(c, buf, n);
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
        size += n;
    }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    crc = c ^ 0xFFFFFFFFu;
    return ok;
}

static std::vector<std::string> stamps(const std::string& gameDir) {
    std::vector<std::string> out;
    for (const std::string& n : listDir(gameDir))
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0 && isStamp(n.substr(0, n.size() - 5)) &&
            isDir(gameDir + "/" + n.substr(0, n.size() - 5)))
            out.push_back(n.substr(0, n.size() - 5));
    std::sort(out.begin(), out.end());
    return out;
}

CaptureOutcome captureFromDir(const std::string& srcRoot, const std::string& dataDir, uint64_t tid, const std::string& name,
                              uint64_t now, int sessionMinutes, int keep, std::string& msg) {
    std::string root = withSlash(srcRoot);
    std::vector<std::pair<std::string, uint64_t>> list;
    listFilesRecursive(root, "", list);
    if (list.empty()) { msg = "capture: the save is empty"; return CAPTURE_EMPTY; }
    uint64_t total = 0;
    for (auto& f : list) total += f.second;
    if (total > CAPTURE_MAX_BYTES) {
        char b[96];
        snprintf(b, sizeof(b), "capture: save too big (%llu MB, limit %llu MB)", (unsigned long long)(total >> 20),
                 (unsigned long long)(CAPTURE_MAX_BYTES >> 20));
        msg = b;
        return CAPTURE_TOO_BIG;
    }

    std::string gameDir = capturesDir(dataDir) + "/" + hex16(tid);
    time_t tt = (time_t)now;
    struct tm* tmv = gmtime(&tt);
    char st[32];
    strftime(st, sizeof(st), "%Y%m%d-%H%M%S", tmv);
    std::string stamp = st;
    for (int i = 2; pathExists(gameDir + "/" + stamp) || pathExists(gameDir + "/" + stamp + ".json"); i++) stamp = std::string(st) + "-" + std::to_string(i);
    std::string part = gameDir + "/" + stamp + ".part";
    removeTree(part);
    makeDirs(part);

    std::vector<CapFile> files;
    for (auto& f : list) {
        std::string dst = part + "/" + f.first;
        size_t slash = dst.rfind('/');
        if (slash != std::string::npos) makeDirs(dst.substr(0, slash));
        CapFile cf{f.first, 0, 0};
        if (!copyFile(root + f.first, dst, cf.size, cf.crc)) {
            removeTree(part);
            msg = "capture: could not copy " + f.first + " (SD card full?)";
            return CAPTURE_FAILED;
        }
        files.push_back(cf);
    }
    std::sort(files.begin(), files.end(), [](const CapFile& a, const CapFile& b) { return a.path < b.path; });

    // same as the previous capture? (nothing saved during that session)
    std::vector<std::string> old = stamps(gameDir);
    if (!old.empty()) {
        std::vector<CapFile> prev;
        readInfo(gameDir + "/" + old.back() + ".json", nullptr, &prev, nullptr);
        std::sort(prev.begin(), prev.end(), [](const CapFile& a, const CapFile& b) { return a.path < b.path; });
        bool same = prev.size() == files.size();
        for (size_t i = 0; same && i < files.size(); i++)
            same = prev[i].path == files[i].path && prev[i].size == files[i].size && prev[i].crc == files[i].crc;
        if (same) {
            removeTree(part);
            msg = "capture: save unchanged since " + old.back() + ", not kept";
            return CAPTURE_UNCHANGED;
        }
    }

    uint64_t bytes = 0;
    cJSON* j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "title_id", hex16(tid).c_str());
    cJSON_AddStringToObject(j, "name", name.c_str());
    cJSON_AddNumberToObject(j, "time", (double)now);
    cJSON_AddNumberToObject(j, "session_minutes", sessionMinutes);
    cJSON* arr = cJSON_AddArrayToObject(j, "files");
    for (const CapFile& f : files) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "path", f.path.c_str());
        cJSON_AddNumberToObject(o, "size", (double)f.size);
        char c[12];
        snprintf(c, sizeof(c), "%08X", f.crc);
        cJSON_AddStringToObject(o, "crc32", c);
        cJSON_AddItemToArray(arr, o);
        bytes += f.size;
    }
    cJSON_AddNumberToObject(j, "bytes", (double)bytes);
    char* txt = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    bool ok = txt && rename(part.c_str(), (gameDir + "/" + stamp).c_str()) == 0;
    if (ok) ok = writeFile(gameDir + "/" + stamp + ".json", std::string(txt) + "\n");
    if (txt) free(txt);
    if (!ok) {
        removeTree(part);
        removeTree(gameDir + "/" + stamp);
        msg = "capture: could not write the capture (SD card full?)";
        return CAPTURE_FAILED;
    }

    // keep the newest `keep`
    std::vector<std::string> all = stamps(gameDir);
    int removed = 0;
    for (size_t i = 0; keep > 0 && all.size() - i > (size_t)keep; i++) {
        removeTree(gameDir + "/" + all[i]);
        remove((gameDir + "/" + all[i] + ".json").c_str());
        removed++;
    }
    char b[160];
    snprintf(b, sizeof(b), "capture: %s saved (%u files, %llu KB, %d min session)%s", stamp.c_str(), (unsigned)files.size(),
             (unsigned long long)(bytes >> 10), sessionMinutes, removed ? ", oldest removed" : "");
    msg = b;
    return CAPTURE_SAVED;
}

// ---------------------------------------------------------------- listing
std::vector<CaptureInfo> listCaptures(const std::string& dataDir, uint64_t tid) {
    std::vector<CaptureInfo> out;
    std::string gameDir = capturesDir(dataDir) + "/" + hex16(tid);
    for (const std::string& s : stamps(gameDir)) {
        CaptureInfo ci;
        ci.stamp = s;
        if (readInfo(gameDir + "/" + s + ".json", &ci, nullptr, nullptr)) out.push_back(ci);
    }
    return out;
}

int deleteCaptures(const std::string& dataDir, uint64_t tid) {
    std::string gameDir = capturesDir(dataDir) + "/" + hex16(tid);
    int n = (int)stamps(gameDir).size();
    removeTree(gameDir);
    return n;
}

std::vector<uint64_t> capturedTitles(const std::string& dataDir) {
    std::vector<uint64_t> out;
    for (const std::string& n : listDir(capturesDir(dataDir)))
        if (isHex16(n) && !stamps(capturesDir(dataDir) + "/" + n).empty()) out.push_back(strtoull(n.c_str(), nullptr, 16));
    std::sort(out.begin(), out.end());
    return out;
}

std::string capturesIndexJson(const std::string& dataDir, const std::map<uint64_t, std::string>& names) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON* games = cJSON_AddArrayToObject(root, "games");
    for (uint64_t tid : capturedTitles(dataDir)) {
        std::string gameDir = capturesDir(dataDir) + "/" + hex16(tid);
        cJSON* g = cJSON_CreateObject();
        cJSON_AddStringToObject(g, "title_id", hex16(tid).c_str());
        std::string name;
        auto it = names.find(tid);
        if (it != names.end()) name = it->second;
        cJSON* caps = cJSON_CreateArray();
        for (const std::string& s : stamps(gameDir)) {
            std::vector<uint8_t> buf;
            if (!readFile(gameDir + "/" + s + ".json", buf)) continue;
            buf.push_back(0);
            cJSON* c = cJSON_Parse((const char*)buf.data());
            if (!c) continue;
            cJSON* n = cJSON_GetObjectItem(c, "name");
            if (name.empty() && cJSON_IsString(n)) name = n->valuestring;
            cJSON_DeleteItemFromObject(c, "name");
            cJSON_DeleteItemFromObject(c, "title_id");
            cJSON_AddStringToObject(c, "stamp", s.c_str());
            cJSON_AddItemToArray(caps, c);
        }
        cJSON_AddStringToObject(g, "name", name.empty() ? hex16(tid).c_str() : name.c_str());
        cJSON_AddItemToObject(g, "captures", caps);
        cJSON_AddItemToArray(games, g);
    }
    char* txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    std::string out = txt ? txt : "{}";
    if (txt) free(txt);
    return out;
}

bool captureFilePath(const std::string& dataDir, const std::string& tidHex, const std::string& stamp, const std::string& rel,
                     std::string& out) {
    if (!isHex16(tidHex) || !isStamp(stamp) || rel.empty() || rel[0] == '/' || rel.size() > 512) return false;
    size_t i = 0;
    while (i <= rel.size()) {   // no "", "." or ".." segment, no control characters or backslashes
        size_t j = rel.find('/', i);
        if (j == std::string::npos) j = rel.size();
        std::string seg = rel.substr(i, j - i);
        if (seg.empty() || seg == "." || seg == "..") return false;
        i = j + 1;
    }
    for (unsigned char c : rel) if (c < 0x20 || c == '\\' || c == 0x7F) return false;
    out = capturesDir(dataDir) + "/" + tidHex + "/" + stamp + "/" + rel;
    struct stat st;
    return stat(out.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
