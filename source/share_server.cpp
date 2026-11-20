#include "share_server.h"
#include "captures.h"
#include "platform.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

bool ShareServer::start(int port, std::string& err) {
    stop();
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { err = "socket() failed"; return false; }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr*)&a, sizeof(a)) != 0) { close(fd); err = "port " + std::to_string(port) + " is busy"; return false; }
    if (listen(fd, 8) != 0) { close(fd); err = "listen() failed"; return false; }
    m_fd = fd;
    return true;
}

void ShareServer::stop() {
    if (m_fd >= 0) close(m_fd);
    m_fd = -1;
}

int ShareServer::poll() {
    int served = 0;
    while (m_fd >= 0 && served < 4) {
        struct pollfd p = {m_fd, POLLIN, 0};
        if (::poll(&p, 1, 0) <= 0 || !(p.revents & POLLIN)) break;
        struct sockaddr_in from;
        socklen_t len = sizeof(from);
        int c = accept(m_fd, (struct sockaddr*)&from, &len);
        if (c < 0) break;
        handle(c);
        close(c);
        served++;
    }
    return served;
}

static bool sendAll(int c, const void* data, size_t n) {
    const char* p = (const char*)data;
    while (n > 0) {
        ssize_t k = send(c, p, n > 65536 ? 65536 : n, MSG_NOSIGNAL);
        if (k <= 0) return false;
        p += k;
        n -= (size_t)k;
    }
    return true;
}

static void sendHead(int c, int code, const char* status, const char* type, uint64_t len) {
    char h[400];
    snprintf(h, sizeof(h),
             "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %llu\r\nCache-Control: no-store\r\n"
             "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
             code, status, type, (unsigned long long)len);
    sendAll(c, h, strlen(h));
}

static void sendText(int c, int code, const char* status, const std::string& body, const char* type = "text/plain; charset=utf-8") {
    sendHead(c, code, status, type, body.size());
    sendAll(c, body.data(), body.size());
}

static std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else out += s[i];
    }
    return out;
}

// Streams a file in chunks (saves can be several MB).
static uint64_t sendFile(int c, const std::string& path, const char* type) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { sendText(c, 404, "Not Found", "not found\n"); return 0; }
    struct stat st;
    uint64_t size = (stat(path.c_str(), &st) == 0) ? (uint64_t)st.st_size : 0;
    sendHead(c, 200, "OK", type, size);
    static char buf[64 * 1024];
    uint64_t sent = 0;
    while (sent < size) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n == 0 || !sendAll(c, buf, n)) break;
        sent += n;
    }
    fclose(f);
    return sent;
}

void ShareServer::handle(int c) {
    struct timeval tv = {3, 0};   // a silent client must not freeze the app
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    std::string req;
    char buf[2048];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 8192) {
        ssize_t n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, (size_t)n);
    }
    size_t sp1 = req.find(' '), sp2 = sp1 == std::string::npos ? sp1 : req.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return;
    std::string method = req.substr(0, sp1), target = req.substr(sp1 + 1, sp2 - sp1 - 1);
    size_t q = target.find('?');
    if (q != std::string::npos) target = target.substr(0, q);
    if (method != "GET" && method != "HEAD") { sendText(c, 405, "Method Not Allowed", "read-only\n"); return; }
    lastPath = target.size() > 80 ? target.substr(0, 77) + "..." : target;

    if (target == "/" || target == "/index.html") {
        uint64_t n = sendFile(c, pagePath, "text/html; charset=utf-8");
        if (n) { requests++; bytesSent += n; }
        return;
    }
    if (target == "/captures.json") {
        std::string j = capturesIndexJson(dataDir, names);
        sendText(c, 200, "OK", j, "application/json");
        requests++;
        bytesSent += j.size();
        return;
    }
    if (target.compare(0, 3, "/c/") == 0) {
        // /c/<TID>/<stamp>/<path...>
        std::string rest = target.substr(3);
        size_t a = rest.find('/'), b = a == std::string::npos ? a : rest.find('/', a + 1);
        if (b != std::string::npos) {
            std::string path;
            if (captureFilePath(dataDir, rest.substr(0, a), rest.substr(a + 1, b - a - 1), urlDecode(rest.substr(b + 1)), path)) {
                uint64_t n = sendFile(c, path, "application/octet-stream");
                if (n) { requests++; bytesSent += n; }
                return;
            }
        }
    }
    sendText(c, 404, "Not Found", "not found\n");
}
