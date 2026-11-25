// Host test of the unlock sounds (include/jingle.h): chime and fanfare length/level per tier, volume, WAV decoding.
//   g++ -std=gnu++17 -Iinclude host/test/test_jingle.cpp source/jingle.cpp -o /tmp/test_jingle && /tmp/test_jingle [out_dir]
// With out_dir, writes chime_<tier>.wav and fanfare_<tier>.wav there to listen to.
#include "jingle.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int fails = 0;
static void ok(bool c, const char* m) {
    if (!c) fails++;
    printf("%s %s\n", c ? "ok  " : "FAIL", m);
}

static void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
static void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static std::vector<uint8_t> wav(int ch, int bits, uint32_t rate, const std::vector<uint8_t>& pcm, bool extraChunk = false) {
    std::vector<uint8_t> v = {'R', 'I', 'F', 'F'};
    put32(v, 0);
    v.insert(v.end(), {'W', 'A', 'V', 'E'});
    if (extraChunk) { v.insert(v.end(), {'L', 'I', 'S', 'T'}); put32(v, 3); v.insert(v.end(), {1, 2, 3, 0}); }   // odd size + pad byte
    v.insert(v.end(), {'f', 'm', 't', ' '});
    put32(v, 16); put16(v, 1); put16(v, (uint16_t)ch); put32(v, rate); put32(v, rate * ch * bits / 8);
    put16(v, (uint16_t)(ch * bits / 8)); put16(v, (uint16_t)bits);
    v.insert(v.end(), {'d', 'a', 't', 'a'});
    put32(v, (uint32_t)pcm.size());
    v.insert(v.end(), pcm.begin(), pcm.end());
    const uint32_t riff = (uint32_t)v.size() - 8;   // RIFF size: whole file minus its first 8 bytes
    for (int i = 0; i < 4; i++) v[4 + i] = (uint8_t)(riff >> (8 * i));
    return v;
}

int main(int argc, char** argv) {
    // --- built-in sounds (chime, fanfare)
    ok(jingleStyleOf("chime") == JINGLE_CHIME && jingleStyleOf("fanfare") == JINGLE_FANFARE && jingleStyleOf("custom") == JINGLE_CHIME,
       "style names (a failed custom sound falls back to the chime)");
    const char* names[] = {"chime", "fanfare"};
    for (int st = 0; st < 2; st++) {
        size_t prev = 0;
        bool longer = true;
        for (int t = 0; t < 4; t++) { size_t f = jingleFrames(st, t); if (f <= prev) longer = false; prev = f; }
        char m[120];
        snprintf(m, sizeof(m), "%s: higher tiers last longer, 0.5 s .. 1.7 s, platinum under 330 KB", names[st]);
        ok(longer && jingleFrames(st, 0) > JINGLE_RATE / 2 && jingleFrames(st, 3) < (size_t)(1.7 * JINGLE_RATE) &&
               jingleFrames(st, 3) * 4 < 330 * 1024, m);
        ok(jingleFrames(st, -4) == jingleFrames(st, 0) && jingleFrames(st, 9) == jingleFrames(st, 3), "tier clamped");
        for (int t = 0; t < 4; t++) {
            std::vector<int16_t> s(jingleFrames(st, t) * 2);
            jingleSynth(st, t, 100, s.data());
            int peak = 0;
            size_t clipped = 0;
            double energyL = 0, energyR = 0;
            for (size_t i = 0; i < s.size(); i++) {
                peak = std::max(peak, std::abs((int)s[i]));
                if (std::abs((int)s[i]) >= 32767) clipped++;
                (i % 2 ? energyR : energyL) += (double)s[i] * s[i];
            }
            snprintf(m, sizeof(m), "%s tier %d: peak %d (about 85 %% of full scale), no clipping", names[st], t, peak);
            ok(peak > 26000 && peak < 29000 && clipped == 0, m);
            ok(energyL > 0 && energyR > 0 && std::fabs(energyL - energyR) / (energyL + energyR) < 0.5, "stereo, both sides audible");
            ok(std::abs((int)s[s.size() - 1]) < 200 && std::abs((int)s[s.size() - 2]) < 200, "fades out (no click at the end)");
            std::vector<int16_t> again(s.size());
            jingleSynth(st, t, 100, again.data());
            ok(again == s, "same sound every time");
            if (argc > 1) {   // write it to listen to
                std::vector<uint8_t> pcm((uint8_t*)s.data(), (uint8_t*)(s.data() + s.size()));
                std::vector<uint8_t> f = wav(2, 16, JINGLE_RATE, pcm);
                std::string path = std::string(argv[1]) + "/" + names[st] + "_" + std::to_string(t) + ".wav";
                FILE* o = fopen(path.c_str(), "wb");
                if (o) { fwrite(f.data(), 1, f.size(), o); fclose(o); }
            }
        }
    }
    std::vector<int16_t> quiet(jingleFrames(JINGLE_CHIME, 2) * 2), loud(quiet.size()), mute(quiet.size());
    jingleSynth(JINGLE_CHIME, 2, 30, quiet.data());
    jingleSynth(JINGLE_CHIME, 2, 100, loud.data());
    jingleSynth(JINGLE_CHIME, 2, 0, mute.data());
    int pq = 0, pl = 0, pm = 0;
    for (size_t i = 0; i < loud.size(); i++) {
        pq = std::max(pq, std::abs((int)quiet[i]));
        pl = std::max(pl, std::abs((int)loud[i]));
        pm = std::max(pm, std::abs((int)mute[i]));
    }
    ok(std::fabs((double)pq / pl - 0.3) < 0.02 && pm == 0, "volume scales the level, 0 = silent");

    // --- WAV decoding
    std::vector<uint8_t> mono8(11025);   // 1 s of 8-bit mono at 11025 Hz: a square wave
    for (size_t i = 0; i < mono8.size(); i++) mono8[i] = (i / 25) % 2 ? 228 : 28;
    std::vector<uint8_t> m8 = wav(1, 8, 11025, mono8);
    std::vector<int16_t> out;
    std::string err;
    ok(wavDecode(m8.data(), m8.size(), 100, 999999, out, err) && out.size() == 2 * 48000, "8-bit mono 11 kHz -> 48 kHz stereo, same duration");
    ok(out[0] == out[1] && std::abs(out[0] + 25600) < 400, "mono copied to both sides, 8-bit scaled");
    std::vector<uint8_t> st16;
    for (int i = 0; i < 48000; i++) { put16(st16, 1000); put16(st16, (uint16_t)-2000); }
    std::vector<uint8_t> f = wav(2, 16, 48000, st16, true);
    ok(wavDecode(f.data(), f.size(), 50, 999999, out, err) && out.size() == 96000 && std::abs(out[0] - 500) <= 1 && std::abs(out[1] + 1000) <= 1,
       "16-bit stereo 48 kHz, extra chunk skipped, volume 50 %");
    ok(wavDecode(f.data(), f.size(), 100, 12000, out, err) && out.size() == 24000, "cut at maxFrames");
    std::vector<uint8_t> bad = wav(2, 16, 48000, st16);
    bad[20] = 3;   // IEEE float format
    ok(!wavDecode(bad.data(), bad.size(), 100, 999999, out, err) && err.find("PCM") != std::string::npos, "float WAV refused");
    const char* junk = "RIFF....AVI LIST";
    ok(!wavDecode((const uint8_t*)junk, strlen(junk), 100, 999999, out, err) && err == "not a WAV file", "not a WAV refused");
    std::vector<uint8_t> trunc = wav(2, 16, 48000, st16);
    trunc.resize(60);   // header + a few bytes of data; the data chunk says more
    ok(wavDecode(trunc.data(), trunc.size(), 100, 999999, out, err) && out.size() > 0 && out.size() < 20, "truncated file: what is there");

    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
