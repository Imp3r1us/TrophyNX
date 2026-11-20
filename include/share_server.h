// Wi-Fi share: a tiny read-only HTTP server, running only while its screen is open in the app.
// It serves the Save Lab page itself (romfs:/savelab.html, so the phone loads it over plain http
// from the Switch and nothing is blocked as mixed content), captures.json and the capture files.
//   GET /                         the Save Lab
//   GET /captures.json            every game with captures (captures.h)
//   GET /c/<TITLEID>/<stamp>/<path>  one file of one capture
// Nothing can be written, and only files inside a capture folder can be read.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>

class ShareServer {
public:
    ~ShareServer() { stop(); }
    bool start(int port, std::string& err);
    void stop();
    bool running() const { return m_fd >= 0; }
    // Handles the connections waiting right now (call once per frame). Returns how many were served.
    int poll();

    std::string dataDir;      // where captures/ lives
    std::string pagePath;     // the Save Lab html
    std::map<uint64_t, std::string> names;   // game names for captures.json
    int requests = 0;         // files served (for the screen)
    uint64_t bytesSent = 0;
    std::string lastPath;

private:
    void handle(int client);
    int m_fd = -1;
};
