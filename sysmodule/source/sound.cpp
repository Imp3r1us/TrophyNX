// Unlock sound: see include/sound.h
#include "sound.h"
#include "heap.h"
#include "jingle.h"
#include "store.h"
#include <switch.h>
#include <malloc.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <vector>

static void (*g_log)(const char*, ...) = nullptr;
#define SLOG(...) do { if (g_log) g_log(__VA_ARGS__); } while (0)
void soundSetLog(void (*logFn)(const char*, ...)) { g_log = logFn; }

namespace {
Thread g_thread;
bool g_running = false;
void* g_buf = nullptr;      // 0x1000-aligned PCM (audout maps it)
size_t g_bufSize = 0, g_dataSize = 0;

void play(void*) {
    Result rc = audoutInitialize();
    if (R_FAILED(rc)) { SLOG("sound: audio output unavailable 0x%X", rc); return; }
    if (audoutGetSampleRate() != (u32)JINGLE_RATE || audoutGetChannelCount() != 2) {
        SLOG("sound: unexpected output format %u Hz x %u", audoutGetSampleRate(), audoutGetChannelCount());
        audoutExit();
        return;
    }
    if (R_SUCCEEDED(rc = audoutStartAudioOut())) {
        AudioOutBuffer ob = {};
        ob.buffer = g_buf;
        ob.buffer_size = g_bufSize;
        ob.data_size = g_dataSize;
        AudioOutBuffer* released = nullptr;
        rc = audoutPlayBuffer(&ob, &released);   // appends and waits until played
        if (R_FAILED(rc)) SLOG("sound: play failed 0x%X", rc);
        audoutStopAudioOut();
    } else SLOG("sound: start failed 0x%X", rc);
    audoutExit();
}

bool allocate(size_t dataBytes, size_t reserve, const char* what) {
    size_t size = (dataBytes + 0xFFF) & ~(size_t)0xFFF;
    if (heapFree() < size + reserve + 64 * 1024) {
        SLOG("sound: %s skipped, %u KB free in the heap", what, (unsigned)(heapFree() >> 10));
        return false;
    }
    g_buf = memalign(0x1000, size);
    if (!g_buf) { SLOG("sound: %s skipped, out of memory", what); return false; }
    memset(g_buf, 0, size);
    g_bufSize = size;
    g_dataSize = dataBytes;
    return true;
}
}  // namespace

void soundJoin() {
    if (g_running) {
        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
        g_running = false;
    }
    free(g_buf);
    g_buf = nullptr;
    g_bufSize = g_dataSize = 0;
}

bool soundStart(const std::string& sound, int volume, int tier, size_t reserve) {
    soundJoin();
    if (sound == "off" || volume <= 0) return false;
    bool ready = false;
    if (sound == "custom") {
        std::string path = dataDir() + "/sounds/unlock.wav";
        struct stat st;
        const size_t maxFrames = (size_t)(JINGLE_MAX_SECONDS * JINGLE_RATE);
        if (stat(path.c_str(), &st) != 0) SLOG("sound: %s not found, playing the chime", path.c_str());
        else if ((size_t)st.st_size > 4 * 1024 * 1024 || heapFree() < (size_t)st.st_size + maxFrames * 4 + reserve + 128 * 1024)
            SLOG("sound: %s too big for the heap (%u KB), playing the chime", path.c_str(), (unsigned)(st.st_size >> 10));
        else {
            std::vector<uint8_t> file((size_t)st.st_size);
            FILE* f = fopen(path.c_str(), "rb");
            size_t got = f ? fread(file.data(), 1, file.size(), f) : 0;
            if (f) fclose(f);
            std::vector<int16_t> pcm;
            std::string err;
            if (got != file.size() || !wavDecode(file.data(), file.size(), volume, maxFrames, pcm, err))
                SLOG("sound: %s: %s, playing the chime", path.c_str(), err.empty() ? "read error" : err.c_str());
            else {
                std::vector<uint8_t>().swap(file);   // free the file before the aligned copy
                if (allocate(pcm.size() * 2, reserve, "custom sound")) {
                    memcpy(g_buf, pcm.data(), pcm.size() * 2);
                    ready = true;
                }
            }
        }
    }
    if (!ready) {
        const int style = jingleStyleOf(sound);   // "custom" that failed -> the chime
        size_t frames = jingleFrames(style, tier);
        if (!allocate(frames * 4, reserve, style == JINGLE_FANFARE ? "fanfare" : "chime")) return false;
        jingleSynth(style, tier, volume, (int16_t*)g_buf);
    }
    // priority 45: just under the popup thread (44), core 3 like the rest of the module
    Result rc = threadCreate(&g_thread, play, nullptr, nullptr, 0x4000, 45, -2);
    if (R_SUCCEEDED(rc) && R_FAILED(rc = threadStart(&g_thread))) threadClose(&g_thread);
    if (R_FAILED(rc)) {
        SLOG("sound: thread failed 0x%X", rc);
        soundJoin();
        return false;
    }
    g_running = true;
    return true;
}
