#include "scan.h"
#include "byml.h"
#include "json_doc.h"
#include "save_formats.h"
#include "cJSON.h"
#include "platform.h"
#include "version.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

static const size_t MAX_FILE_COPY = 4u << 20;    // copy readable saves up to 4 MB each (Odyssey: 2,097,164 bytes)...
static const size_t MAX_TOTAL_COPY = 17u << 20;  // ...and 17 MB in all (bundle < 25 MB once base64-encoded: GitHub web upload limit)
static const size_t MAX_READ = 32u << 20;        // never load bigger files (still listed, format "binary")

static double entropyOf(const std::vector<uint8_t>& d, size_t n) {
    if (!n) return 0;
    size_t cnt[256] = {0};
    for (size_t i = 0; i < n; i++) cnt[d[i]]++;
    double h = 0;
    for (size_t c : cnt) if (c) { double p = (double)c / n; h -= p * std::log2(p); }
    return h;
}

std::string scanFormat(const std::vector<uint8_t>& data, double* entropyOut) {
    size_t n = std::min<size_t>(data.size(), 65536);
    double e = entropyOf(data, n);
    if (entropyOut) *entropyOut = e;
    if (data.empty()) return "empty";
    {   // BYML at 0 or 0x10
        for (size_t b : {(size_t)0, (size_t)0x10}) {
            if (data.size() >= b + 0x10 && ((data[b] == 'Y' && data[b + 1] == 'B') || (data[b] == 'B' && data[b + 1] == 'Y'))) {
                BymlDoc d;
                std::vector<uint8_t> copy(data);
                if (d.parse(std::move(copy))) return "byml";
            }
        }
    }
    if (isGvas(data.data(), data.size()) || isNrbf(data.data(), data.size())) {   // Unreal / .NET (Unity) saves
        bool gv = isGvas(data.data(), data.size());
        std::string err;
        cJSON* t = gv ? gvasToJson(data.data(), data.size(), err) : nrbfToJson(data.data(), data.size(), err);
        if (t) { cJSON_Delete(t); return gv ? "gvas" : "nrbf"; }
    }
    size_t s = 0;
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) s = 3;
    while (s < data.size() && (data[s] == ' ' || data[s] == '\t' || data[s] == '\r' || data[s] == '\n')) s++;
    if (s < data.size() && (data[s] == '{' || data[s] == '[')) {
        JsonDoc j;
        std::vector<uint8_t> copy(data);
        if (j.parse(std::move(copy))) return "json";
    }
    {   // text XORed with a known key (SIGNALIS lstr.replika): copied as is, the Save Lab finds the key again
        size_t hi = 0;
        for (size_t i = 0; i < n; i++) if (data[i] >= 0x80) hi++;
        if (data.size() >= 64 && hi <= n / 100) {
            for (const std::string& k : knownXorKeys()) {
                std::vector<uint8_t> copy(data);
                for (size_t i = 0; i < copy.size(); i++) copy[i] ^= (uint8_t)k[i % k.size()];
                JsonDoc j;
                if (j.parse(std::move(copy))) return "json-xor";
            }
        }
    }
    const uint8_t* p = data.data();
    if (data.size() >= 4) {
        if ((p[0] == 0x78 && (p[1] == 0x01 || p[1] == 0x5E || p[1] == 0x9C || p[1] == 0xDA)) ||
            (p[0] == 0x28 && p[1] == 0xB5 && p[2] == 0x2F && p[3] == 0xFD) || (p[0] == 0x1F && p[1] == 0x8B) ||
            (p[0] == 0x04 && p[1] == 0x22 && p[2] == 0x4D && p[3] == 0x18) || (p[0] == 'Y' && p[1] == 'a' && p[2] == 'z' && p[3] == '0') ||
            (p[0] == 'P' && p[1] == 'K' && p[2] == 3 && p[3] == 4))
            return "compressed";
    }
    size_t printable = 0;
    for (size_t i = 0; i < n; i++) { uint8_t c = data[i]; if (c == 9 || c == 10 || c == 13 || (c >= 32 && c != 127)) printable++; }
    if (n && printable >= n * 97 / 100 && e < 6.5) return s < data.size() && data[s] == '<' ? "xml" : "text";
    if (e >= 7.6) return "encrypted";
    return "binary";
}

std::string scanVerdict(const std::vector<std::string>& formats, bool hasReader) {
    if (hasReader) return "supported";
    if (formats.empty()) return "no-save";
    bool tree = false, text = false, binary = false;
    for (const std::string& f : formats) {
        if (f == "json" || f == "json-xor" || f == "byml" || f == "gvas" || f == "nrbf") tree = true;
        else if (f == "xml" || f == "text") text = true;
        else if (f == "binary") binary = true;
    }
    if (tree) return "draftable";
    if (text) return "text";
    if (binary) return "binary";
    return "encrypted";
}

// ------------------------------------------------------------------ streaming JSON writer
static void jstr(FILE* f, const std::string& s) {
    fputc('"', f);
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}
static void b64(FILE* f, const std::vector<uint8_t>& d) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char buf[4096];
    size_t o = 0;
    for (size_t i = 0; i < d.size(); i += 3) {
        uint32_t v = d[i] << 16 | (i + 1 < d.size() ? d[i + 1] << 8 : 0) | (i + 2 < d.size() ? d[i + 2] : 0);
        buf[o++] = T[v >> 18 & 63];
        buf[o++] = T[v >> 12 & 63];
        buf[o++] = i + 1 < d.size() ? T[v >> 6 & 63] : '=';
        buf[o++] = i + 2 < d.size() ? T[v & 63] : '=';
        if (o >= sizeof(buf) - 4) { fwrite(buf, 1, o, f); o = 0; }
    }
    fwrite(buf, 1, o, f);
}

bool runCompatibilityScan(Library& lib, plat::ProgressFn progress, void* ctx, ScanSummary& out) {
    out = ScanSummary();
    std::string dir = plat::dataDir();
    out.bundlePath = dir + "/scan_bundle.json";
    std::string tmp = out.bundlePath + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) { out.error = "cannot write " + tmp; return false; }
    std::string report = "Compatibility scan (Switch Achievements " APP_VERSION ")\n"
                         "supported = save-based set exists | draftable = JSON (also XORed)/BYML/GVAS/NRBF, drafts can be generated | text = XML/text, reader needed\n"
                         "binary = needs captures in Save Lab | encrypted = compressed or encrypted, not readable | no-save = nothing to read\n\n";
    fprintf(f, "{\"format\":\"switch-achievements-scan\",\"version\":1,\"app_version\":\"%s\",\"generated\":%llu,\"user\":",
            APP_VERSION, (unsigned long long)plat::now());
    jstr(f, plat::userKey());
    fputs(",\"games\":[\n", f);
    int total = (int)lib.games.size() - 1, done = 0;
    bool firstGame = true;
    for (size_t gi = 1; gi < lib.games.size(); gi++) {
        Game& g = lib.games[gi];
        if (progress) progress(done++, total, ctx);
        out.games++;
        std::vector<std::string> formats;
        char idhex[17];
        snprintf(idhex, sizeof(idhex), "%016llX", (unsigned long long)g.id);
        fputs(firstGame ? "" : ",\n", f);
        firstGame = false;
        fputs("{\"title_id\":", f); jstr(f, idhex);
        fputs(",\"name\":", f); jstr(f, g.name);
        fputs(",\"reader\":", f); jstr(f, g.reader);
        std::string files, err;
        if (!plat::openSave(g.id)) {
            err = plat::lastError();
        } else {
            std::vector<std::pair<std::string, uint64_t>> list;
            plat::listSaveFiles(list);
            out.withSave++;
            fputs(",\"files\":[", f);
            bool firstFile = true;
            for (auto& e : list) {
                std::vector<uint8_t> data;
                std::string fmt = "binary";
                double ent = 0;
                bool loaded = e.second <= MAX_READ && plat::readSaveFile(e.first, data);
                if (loaded) fmt = scanFormat(data, &ent);
                formats.push_back(fmt);
                fputs(firstFile ? "" : ",", f);
                firstFile = false;
                fputs("{\"path\":", f); jstr(f, e.first);
                fprintf(f, ",\"size\":%llu,\"format\":\"%s\",\"entropy\":%.2f", (unsigned long long)e.second, fmt.c_str(), ent);
                bool readable = fmt == "json" || fmt == "json-xor" || fmt == "byml" || fmt == "gvas" || fmt == "nrbf" || fmt == "xml" || fmt == "text";
                if (loaded && readable && data.size() <= MAX_FILE_COPY && out.bytesCopied + data.size() <= MAX_TOTAL_COPY) {
                    fputs(",\"data_b64\":\"", f); b64(f, data); fputc('"', f);
                    out.bytesCopied += data.size();
                } else if (readable) fputs(",\"skipped\":\"size limit\"", f);
                fputc('}', f);
                char line[160];
                snprintf(line, sizeof(line), "    %s  %llu bytes  %s\n", e.first.c_str(), (unsigned long long)e.second, fmt.c_str());
                files += line;
            }
            fputc(']', f);
            plat::closeSave();
        }
        std::string verdict = err.empty() ? scanVerdict(formats, !g.reader.empty()) : "no-save";
        if (verdict == "supported") out.supported++; else if (verdict == "draftable") out.draftable++;
        else if (verdict == "text") out.text++; else if (verdict == "binary") out.binary++; else if (verdict == "encrypted") out.encrypted++;
        fputs(",\"verdict\":", f); jstr(f, verdict);
        if (!err.empty()) { fputs(",\"error\":", f); jstr(f, err); }
        fputc('}', f);
        report += std::string(idhex) + "  " + g.name + "  ->  " + verdict + (g.reader.empty() ? "" : " (" + g.reader + ")") + (err.empty() ? "" : "  [" + err + "]") + "\n" + files;
    }
    fputs("\n]}\n", f);
    bool ok = !ferror(f);
    fclose(f);
    if (!ok) { out.error = "write error (SD card full?)"; remove(tmp.c_str()); return false; }
    remove(out.bundlePath.c_str());
    if (rename(tmp.c_str(), out.bundlePath.c_str()) != 0) { out.error = "cannot rename " + tmp; return false; }
    char sum[256];
    snprintf(sum, sizeof(sum), "\n%d games, %d with a save: %d supported, %d draftable, %d text, %d binary, %d encrypted. %zu KB of saves copied.\n",
             out.games, out.withSave, out.supported, out.draftable, out.text, out.binary, out.encrypted, out.bytesCopied / 1024);
    report += sum;
    std::vector<uint8_t> rb(report.begin(), report.end());
    FILE* r = fopen((dir + "/scan_report.txt").c_str(), "wb");
    if (r) { fwrite(rb.data(), 1, rb.size(), r); fclose(r); }
    if (progress) progress(total, total, ctx);
    return true;
}
