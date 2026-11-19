#include "store.h"
#include "cJSON.h"
#include <cstdio>
#include <sys/stat.h>
#include <vector>

const char* const DATA_DIR = "sdmc:/switch/SwitchAchievements";

std::string userKeyOf(const AccountUid& uid) {
    char b[40];
    snprintf(b, sizeof(b), "%016lx%016lx", (unsigned long)uid.uid[1], (unsigned long)uid.uid[0]);
    return b;
}

std::string lastUser(AccountUid* uidOut) {
    AccountUid uid = {};
    if (R_FAILED(accountGetLastOpenedUser(&uid)) || !accountUidIsValid(&uid)) return "";
    if (uidOut) *uidOut = uid;
    return userKeyOf(uid);
}

static std::string storePath(const std::string& user) { return std::string(DATA_DIR) + "/unlocks/" + user + ".json"; }

std::map<std::string, uint64_t> loadStore(const std::string& user) {
    std::map<std::string, uint64_t> m;
    FILE* f = fopen(storePath(user).c_str(), "rb");
    if (!f) return m;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string buf(sz > 0 ? sz : 0, '\0');
    if (sz > 0 && fread(&buf[0], 1, sz, f) != (size_t)sz) buf.clear();
    fclose(f);
    cJSON* root = cJSON_Parse(buf.c_str());
    cJSON* it;
    if (root) {
        cJSON_ArrayForEach(it, root) if (cJSON_IsNumber(it) && it->string) m[it->string] = (uint64_t)it->valuedouble;
        cJSON_Delete(root);
    }
    return m;
}

void saveStore(const std::string& user, const std::map<std::string, uint64_t>& m) {
    mkdir(DATA_DIR, 0777);
    mkdir((std::string(DATA_DIR) + "/unlocks").c_str(), 0777);
    std::string out = "{\n";
    size_t i = 0;
    for (auto& kv : m) {
        char b[32];
        snprintf(b, sizeof(b), "%llu", (unsigned long long)kv.second);
        out += "  \"" + kv.first + "\": " + b + (++i < m.size() ? ",\n" : "\n");
    }
    out += "}\n";
    std::string path = storePath(user), tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    if (!ok) { remove(tmp.c_str()); return; }
    remove(path.c_str());
    rename(tmp.c_str(), path.c_str());
}
