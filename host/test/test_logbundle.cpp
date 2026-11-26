// Host test of "Send logs" (include/logbundle.h): what is collected (and what never is), bundle round trip, refused
// names, receiver address parsing. With a URL argument it also POSTs the bundle (plat::httpPost, PC sockets) to a
// running tools/logserver/trophynx_logserver.py and expects HTTP 200.
//   g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_logbundle.cpp source/logbundle.cpp source/fileutil.cpp source/platform_host.cpp ... (see CI)
#include "logbundle.h"
#include "platform.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

static int fails = 0;
static void ok(bool c, const char* m) {
    if (!c) fails++;
    printf("%s %s\n", c ? "ok  " : "FAIL", m);
}

static const logbundle::File* find(const std::vector<logbundle::File>& v, const char* n) {
    for (const auto& f : v) if (f.name == n) return &f;
    return nullptr;
}

int main(int argc, char** argv) {
    using namespace logbundle;
    // a fake SD card
    std::string root = "/tmp/tnx_logbundle_sd/";
    std::string data = root + "switch/TrophyNX";
    makeDirs(data + "/overlay");
    makeDirs(data + "/unlocks");
    makeDirs(data + "/keys");
    makeDirs(data + "/captures");
    makeDirs(root + "atmosphere/contents/0100633007D48000/exefs");
    makeDirs(root + "atmosphere/contents/420000000000AC01");
    writeFile(data + "/live_log.txt", "[11-26 00:29:11] hook: ring found\n");
    writeFile(data + "/log.txt", std::string(3000, 'x') + "END");
    writeFile(data + "/popup.json", "{\"x\":1}");
    writeFile(data + "/scan_bundle.json", "{\"saves\": \"big\"}");
    writeFile(data + "/keys/theatrhythm.key", "secret password");
    writeFile(data + "/captures/0100.json", "{}");
    writeFile(data + "/overlay/0100633007D48000.memtest.json", "{\"t\":1}");
    writeFile(data + "/unlocks/0123.json", "{\"0100633007D48000/charm_first\": 1}");
    writeFile(data + "/icon.jpg", "\xFF\xD8");
    writeFile(root + "atmosphere/contents/0100633007D48000/trophynx-hook.json", "{\"title_id\": \"0100633007D48000\"}");

    std::vector<File> fs = collect(data, root, "TrophyNX test\n", 1000);
    ok(fs.size() == 7 && fs[0].name == "info.txt", "7 files: info, 3 logs/settings, overlay, unlocks, hook manifest");
    ok(find(fs, "live_log.txt") && find(fs, "popup.json") && find(fs, "overlay/0100633007D48000.memtest.json") &&
           find(fs, "unlocks/0123.json") && find(fs, "hook/0100633007D48000.json"), "logs, overlay, unlocks, hook manifest");
    ok(!find(fs, "scan_bundle.json") && !find(fs, "keys/theatrhythm.key") && !find(fs, "icon.jpg") &&
           !find(fs, "captures/0100.json"), "never: save copies, passwords, captures, images");
    const File* log = find(fs, "log.txt");
    ok(log && log->truncated && log->data.size() == 1000 && log->data.compare(997, 3, "END") == 0, "big log: its newest 1000 bytes");
    for (const File& f : fs) if (f.data.find("secret password") != std::string::npos) ok(false, "password leaked");

    std::string b = encode(fs);
    std::vector<File> back;
    ok(decode(b, back) && back.size() == fs.size(), "round trip");
    bool same = true;
    for (size_t i = 0; i < fs.size() && i < back.size(); i++) same &= fs[i].name == back[i].name && fs[i].data == back[i].data;
    ok(same, "same names and bytes");
    ok(!decode("TNXLOGS1\n../etc/passwd\t3\nabc\n", back), "path escape refused");
    ok(!decode("TNXLOGS1\nlog.txt\t99\nabc\n", back), "size past the end refused");
    ok(!decode("HELLO", back), "wrong magic refused");
    ok(safeName("overlay/a.json") && !safeName("/a") && !safeName("a//b") && !safeName("a/../b") && !safeName("a b"), "safe names");

    std::string url, err;
    ok(serverUrl("192.168.1.20\n", url, err) && url == "http://192.168.1.20:8421/trophynx/logs", "address alone: default port");
    ok(serverUrl("# my pi\n\n  pi.local:9000  \n", url, err) && url == "http://pi.local:9000/trophynx/logs", "comment, spaces, port");
    ok(serverUrl("http://10.0.0.2:80/whatever", url, err) && url == "http://10.0.0.2:80/trophynx/logs", "http:// and a path");
    ok(!serverUrl("https://x", url, err) && !serverUrl("", url, err) && !serverUrl("1.2.3.4:99999", url, err) &&
           !serverUrl("a;rm -rf", url, err), "refused: https, empty, bad port, bad characters");

    if (argc > 1) {   // end to end against a running receiver
        std::string resp;
        err.clear();
        int code = plat::httpPost(argv[1], {"Content-Type: application/octet-stream", "X-TrophyNX: test"}, b, resp, err);
        printf("  receiver: HTTP %d %s%s", code, resp.c_str(), err.empty() ? "" : (err + "\n").c_str());
        ok(code == 200 && resp.find("7 files") != std::string::npos, "POST to the receiver");
    }
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
