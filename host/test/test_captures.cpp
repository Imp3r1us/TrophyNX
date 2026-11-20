// Save captures (captures.cpp) and the Wi-Fi share server (share_server.cpp), on a PC.
//   g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_captures.cpp source/captures.cpp source/share_server.cpp
//       source/fileutil.cpp -x c source/cJSON.c -o /tmp/test_captures -lpthread && /tmp/test_captures
// The server part needs curl (skipped if it isn't installed).
#include "captures.h"
#include "share_server.h"
#include "platform.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static std::string sh(const std::string& cmd) {
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), p)) > 0) out.append(b, n);
    pclose(p);
    return out;
}

int main() {
    char tmpl[] = "/tmp/capXXXXXX";
    std::string root = mkdtemp(tmpl);
    std::string save = root + "/save", data = root + "/data";
    makeDirs(save + "/slot_00");
    writeFile(save + "/slot_00/progress.sav", std::string(5000, 'a'));
    writeFile(save + "/option.sav", "opt");
    const uint64_t TID = 0x0100535012974000ull;

    // settings
    CaptureSettings s = loadCaptureSettings(data);
    CHECK(s.games.empty() && s.keep == 20);
    setCaptureEnabled(s, TID, true);
    s.keep = 2;
    CHECK(saveCaptureSettings(data, s));
    CaptureSettings s2 = loadCaptureSettings(data);
    CHECK(captureEnabled(s2, TID) && s2.keep == 2);
    setCaptureEnabled(s2, TID, false);
    CHECK(!captureEnabled(s2, TID));

    // captures
    std::string msg;
    CHECK(captureFromDir(save, data, TID, "Hades", 1763650000, 12, 2, msg) == CAPTURE_SAVED);
    printf("%s\n", msg.c_str());
    CHECK(captureFromDir(save + "/", data, TID, "Hades", 1763650300, 5, 2, msg) == CAPTURE_UNCHANGED);
    printf("%s\n", msg.c_str());
    writeFile(save + "/slot_00/progress.sav", std::string(5000, 'b'));
    CHECK(captureFromDir(save, data, TID, "Hades", 1763650600, 7, 2, msg) == CAPTURE_SAVED);
    writeFile(save + "/slot_00/progress.sav", std::string(6000, 'c'));
    CHECK(captureFromDir(save, data, TID, "Hades", 1763650600, 9, 2, msg) == CAPTURE_SAVED);   // same second: -2 suffix
    printf("%s\n", msg.c_str());
    std::vector<CaptureInfo> l = listCaptures(data, TID);
    CHECK(l.size() == 2);   // keep = 2: the first one was removed
    if (l.size() == 2) {
        CHECK(l[0].stamp == "20251120-145640" && l[1].stamp == "20251120-145640-2");
        CHECK(l[0].sessionMinutes == 7 && l[1].sessionMinutes == 9);
        CHECK(l[1].bytes == 6003 && l[1].files == 2);
    }
    CHECK(!pathExists(data + "/captures/0100535012974000/20251120-145640.part"));
    std::vector<uint8_t> f;
    CHECK(readFile(data + "/captures/0100535012974000/20251120-145640-2/slot_00/progress.sav", f) && f.size() == 6000 && f[0] == 'c');
    CHECK(capturedTitles(data).size() == 1);

    std::string j = capturesIndexJson(data, {});
    CHECK(j.find("\"name\":\"Hades\"") != std::string::npos && j.find("\"stamp\":\"20251120-145640-2\"") != std::string::npos);
    CHECK(j.find("slot_00/progress.sav") != std::string::npos);
    j = capturesIndexJson(data, {{TID, "Hades (app name)"}});
    CHECK(j.find("Hades (app name)") != std::string::npos);

    // path checks
    std::string p;
    CHECK(captureFilePath(data, "0100535012974000", "20251120-145640", "slot_00/progress.sav", p));
    CHECK(!captureFilePath(data, "0100535012974000", "20251120-145640", "../20251120-145640-2/option.sav", p));
    CHECK(!captureFilePath(data, "0100535012974000", "20251120-145640", "/etc/passwd", p));
    CHECK(!captureFilePath(data, "0100535012974000", "..", "settings.json", p));
    CHECK(!captureFilePath(data, "..", "20251120-145640", "option.sav", p));
    CHECK(!captureFilePath(data, "0100535012974000", "20251120-145640", "slot_00", p));   // a folder

    // server
    if (sh("command -v curl").empty()) printf("curl not installed: server test skipped\n");
    else {
        writeFile(root + "/page.html", "<!DOCTYPE html><title>Save Lab</title>");
        ShareServer srv;
        srv.dataDir = data;
        srv.pagePath = root + "/page.html";
        srv.names[TID] = "Hades";
        std::string err;
        int port = 18000 + getpid() % 1000;
        CHECK(srv.start(port, err));
        std::atomic<bool> stop(false);
        std::thread t([&] { while (!stop) { srv.poll(); usleep(2000); } });
        std::string base = "curl -s --max-time 5 http://127.0.0.1:" + std::to_string(port);
        CHECK(sh(base + "/").find("Save Lab") != std::string::npos);
        std::string idx = sh(base + "/captures.json");
        CHECK(idx.find("\"games\"") != std::string::npos && idx.find("Hades") != std::string::npos);
        std::string file = sh(base + "/c/0100535012974000/20251120-145640-2/slot_00/progress.sav");
        CHECK(file.size() == 6000 && file[0] == 'c');
        CHECK(sh(base + "/c/0100535012974000/20251120-145640-2/option.sav") == "opt");
auto code = [&](const std::string& opts, const std::string& path) { return sh("curl -s --max-time 5 -o /dev/null -w '%{http_code}' " + opts + " 'http://127.0.0.1:" + std::to_string(port) + path + "'"); };
        CHECK(code("--path-as-is", "/c/0100535012974000/20251120-145640/../../settings.json") == "404");
        CHECK(code("--path-as-is", "/c/0100535012974000/20251120-145640/%2e%2e/x") == "404");
        CHECK(code("-X POST", "/captures.json") == "405");
        CHECK(code("", "/nothing") == "404");
        stop = true;
        t.join();
        CHECK(srv.requests >= 4);
        srv.stop();
    }

    CHECK(deleteCaptures(data, TID) == 2);
    CHECK(listCaptures(data, TID).empty());
    sh("rm -rf " + root);
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
