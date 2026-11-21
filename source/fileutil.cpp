#include "platform.h"
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

bool isCleanText(const std::string& s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0 || (n == 0 && c < 0x20) || c == 0x7F) return false;
        if (i + n >= s.size() + (n ? 0 : 1)) return false;
        for (int k = 1; k <= n; k++) if ((s[i + k] & 0xC0) != 0x80) return false;
        i += n + 1;
    }
    return true;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return false; }
    out.resize((size_t)sz);
    size_t rd = sz ? fread(out.data(), 1, (size_t)sz, f) : 0;
    fclose(f);
    return rd == (size_t)sz;
}

bool writeFile(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    if (!ok) { remove(tmp.c_str()); return false; }
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}

void makeDirs(const std::string& path) {
    // Skip the device prefix ("sdmc:/") when creating each level.
    size_t start = path.find(":/");
    start = (start == std::string::npos) ? 1 : start + 2;
    for (size_t i = start; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            std::string sub = path.substr(0, i);
            if (!sub.empty()) mkdir(sub.c_str(), 0777);
        }
    }
}

std::vector<std::string> listDir(const std::string& path) {
    std::vector<std::string> out;
    DIR* d = opendir(path.c_str());
    if (!d) return out;
    while (struct dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n != "." && n != "..") out.push_back(n);
    }
    closedir(d);
    return out;
}

// Lists regular files under root (relative paths with '/', sizes). Depth and count are capped.
void listFilesRecursive(const std::string& root, const std::string& rel, std::vector<std::pair<std::string, uint64_t>>& out, int depth) {
    if (depth > 8 || out.size() >= 2000) return;
    std::string dirPath = root + rel;
    DIR* d = opendir(dirPath.c_str());
    if (!d) return;
    std::vector<std::string> names;
    while (struct dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n != "." && n != "..") names.push_back(n);
    }
    closedir(d);
    for (const std::string& n : names) {
        std::string r = rel + n;
        struct stat st;
        if (stat((root + r).c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) listFilesRecursive(root, r + "/", out, depth + 1);
        else if (out.size() < 2000) out.emplace_back(r, (uint64_t)st.st_size);
    }
}

void findFilesNamed(const std::string& dir, const std::string& name, std::vector<std::string>& out, int depth) {
    if (depth > 16 || out.size() >= 16) return;
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> names;
    while (struct dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n != "." && n != "..") names.push_back(n);
    }
    closedir(d);
    for (const std::string& n : names) {
        std::string p = dir + "/" + n;
        struct stat st;
        if (stat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) findFilesNamed(p, name, out, depth + 1);
        else if (n == name && out.size() < 16) out.push_back(p);
    }
}

bool pathExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::string migrateLegacyDir(const std::string& oldDir, const std::string& newDir) {
    if (pathExists(newDir) || !pathExists(oldDir)) return "";
    if (rename(oldDir.c_str(), newDir.c_str()) == 0) return "Moved " + oldDir + " to " + newDir + ".";
    std::vector<std::pair<std::string, uint64_t>> files;
    listFilesRecursive(oldDir + "/", "", files);
    size_t ok = 0;
    for (auto& f : files) {
        std::vector<uint8_t> buf;
        std::string dst = newDir + "/" + f.first;
        size_t s = dst.find_last_of('/');
        makeDirs(dst.substr(0, s));
        if (readFile(oldDir + "/" + f.first, buf) && writeFile(dst, std::string((const char*)buf.data(), buf.size()))) ok++;
    }
    makeDirs(newDir);
    return "Copied " + std::to_string(ok) + "/" + std::to_string(files.size()) + " files from " + oldDir + " to " + newDir + " (old folder kept).";
}
