// SPDX-License-Identifier: GPL-2.0-or-later
// "Send logs": see include/logbundle.h
#include "logbundle.h"
#include "platform.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace logbundle {

static bool endsWith(const std::string& s, const char* suf) {
    size_t n = strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

static void add(std::vector<File>& out, const std::string& path, const std::string& name, size_t maxPerFile) {
    std::vector<uint8_t> b;
    if (!readFile(path, b)) return;
    File f;
    f.name = name;
    if (b.size() > maxPerFile) {
        f.truncated = true;
        f.data.assign(b.end() - (long)maxPerFile, b.end());
    } else {
        f.data.assign(b.begin(), b.end());
    }
    out.push_back(std::move(f));
}

std::vector<File> collect(const std::string& dataDir, const std::string& sdRoot, const std::string& info,
                          size_t maxPerFile) {
    std::vector<File> out;
    File i;
    i.name = "info.txt";
    i.data = info;
    out.push_back(i);
    // Logs and small status files of the data folder. Not sent: scan_bundle.json (copies of saves, up to 17 MB),
    // keys/ (save passwords), captures/ (save copies).
    std::vector<std::string> top = listDir(dataDir);
    std::sort(top.begin(), top.end());
    for (const std::string& n : top) {
        if (n == "scan_bundle.json" || !(endsWith(n, ".txt") || endsWith(n, ".json") || endsWith(n, ".log"))) continue;
        add(out, dataDir + "/" + n, n, maxPerFile);
    }
    for (const char* sub : {"overlay", "unlocks"}) {
        std::vector<std::string> l = listDir(dataDir + "/" + sub);
        std::sort(l.begin(), l.end());
        for (const std::string& n : l)
            if (endsWith(n, ".json")) add(out, dataDir + "/" + sub + "/" + n, std::string(sub) + "/" + n, maxPerFile);
    }
    // The injected hook's manifests (tools/unityhook hook-make): which games have it, which build, which npdm.
    std::string contents = sdRoot + "atmosphere/contents";
    std::vector<std::string> tids = listDir(contents);
    std::sort(tids.begin(), tids.end());
    for (const std::string& t : tids) {
        std::string p = contents + "/" + t + "/trophynx-hook.json";
        if (t.size() == 16 && pathExists(p)) add(out, p, "hook/" + t + ".json", maxPerFile);
    }
    for (File& f : out)
        if (!safeName(f.name)) f.name = "file_" + std::to_string(&f - &out[0]);
    return out;
}

std::string encode(const std::vector<File>& files) {
    std::string s = "TNXLOGS1\n";
    for (const File& f : files) {
        s += f.name + "\t" + std::to_string(f.data.size()) + "\n";
        s += f.data;
        s += "\n";
    }
    return s;
}

bool decode(const std::string& b, std::vector<File>& out) {
    out.clear();
    if (b.compare(0, 9, "TNXLOGS1\n") != 0) return false;
    size_t p = 9;
    while (p < b.size()) {
        size_t nl = b.find('\n', p), tab = b.find('\t', p);
        if (nl == std::string::npos || tab == std::string::npos || tab > nl) return false;
        File f;
        f.name = b.substr(p, tab - p);
        std::string num = b.substr(tab + 1, nl - tab - 1);
        if (num.empty() || num.size() > 10 || num.find_first_not_of("0123456789") != std::string::npos) return false;
        size_t n = (size_t)strtoull(num.c_str(), nullptr, 10);
        p = nl + 1;
        if (n > b.size() - p || p + n >= b.size() || b[p + n] != '\n' || !safeName(f.name)) return false;
        f.data = b.substr(p, n);
        p += n + 1;
        out.push_back(std::move(f));
    }
    return true;
}

bool safeName(const std::string& n) {
    if (n.empty() || n.size() > 160 || n[0] == '/' || n.back() == '/') return false;
    size_t start = 0;
    while (start <= n.size()) {
        size_t e = n.find('/', start);
        if (e == std::string::npos) e = n.size();
        std::string part = n.substr(start, e - start);
        if (part.empty() || part == "." || part == "..") return false;
        for (char c : part)
            if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')) return false;
        start = e + 1;
    }
    return true;
}

bool serverUrl(const std::string& text, std::string& url, std::string& err) {
    std::string line;
    size_t p = 0;
    while (p < text.size()) {   // first line that is not blank or a comment
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(p, e - p);
        p = e + 1;
        size_t h = l.find('#');
        if (h != std::string::npos) l.erase(h);
        while (!l.empty() && isspace((unsigned char)l.back())) l.pop_back();
        size_t s = 0;
        while (s < l.size() && isspace((unsigned char)l[s])) s++;
        l.erase(0, s);
        if (!l.empty()) { line = l; break; }
    }
    if (line.empty()) { err = "log_server.txt is empty"; return false; }
    if (line.compare(0, 7, "http://") == 0) line.erase(0, 7);
    else if (line.find("://") != std::string::npos) { err = "only http:// is supported"; return false; }
    size_t slash = line.find('/');
    if (slash != std::string::npos) line.erase(slash);   // any path is replaced by /trophynx/logs
    std::string host = line, port = std::to_string(kDefaultPort);
    size_t colon = line.rfind(':');
    if (colon != std::string::npos) {
        host = line.substr(0, colon);
        port = line.substr(colon + 1);
        if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos ||
            atoi(port.c_str()) < 1 || atoi(port.c_str()) > 65535) { err = "bad port in log_server.txt"; return false; }
    }
    if (host.empty()) { err = "no address in log_server.txt"; return false; }
    for (char c : host)
        if (!(isalnum((unsigned char)c) || c == '.' || c == '-')) { err = "bad address in log_server.txt"; return false; }
    url = "http://" + host + ":" + port + "/trophynx/logs";
    return true;
}

}  // namespace logbundle
