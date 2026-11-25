// Achievement unlock sounds: see include/jingle.h
#include "jingle.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

// Two built-in sounds, both made of timed events mixed straight into the output buffer:
//  - chime:   a rising whoosh, a soft low thump, then a warm two-note bell chime (up a fifth) with a long
//             shimmering tail. Higher tiers add sparkle notes above it.
//  - fanfare: a fast rising arpeggio, then a held chord with a little 8-bit sparkle.
// Original sounds (no sample or melody from any console or store). Notes: semitones from A4 (440 Hz).
namespace {
const double kTwoPi = 6.283185307179586;

enum Kind { BELL, CHIME, SUB, WHOOSH };
struct Event { Kind kind; double start, len; int semi; float amp; float pan; };   // seconds; pan -1 left .. 1 right

double freqOf(int semi) { return 440.0 * std::pow(2.0, semi / 12.0); }

// Fanfare voice: sine + a few odd harmonics (a hint of 8-bit sparkle), fast attack, exponential decay.
float bell(double t, double len, double f) {
    double env = t < 0.006 ? t / 0.006 : std::exp(-(t - 0.006) * (3.2 / (len + 0.15)));
    double p = kTwoPi * f * t;
    return (float)((std::sin(p) + 0.28 * std::sin(3 * p) + 0.12 * std::sin(5 * p) + 0.18 * std::sin(2 * p + 0.3)) * env);
}

// Chime voice: a soft electric-piano / glass bell. Fundamental with a slightly detuned twin (gentle chorus), an
// octave that fades faster, and a short inharmonic "ping" (x2.76) that gives the strike its sparkle.
float chime(double t, double len, double f) {
    double env = t < 0.004 ? t / 0.004 : std::exp(-(t - 0.004) * (2.2 / (len + 0.1)));
    double p = kTwoPi * f * t;
    double s = std::sin(p) + 0.55 * std::sin(p * 1.0035 + 0.7) + 0.45 * std::sin(2 * p) * std::exp(-t * 5.0) +
               0.30 * std::sin(2.76 * p) * std::exp(-t * 14.0) + 0.10 * std::sin(4.07 * p) * std::exp(-t * 22.0);
    return (float)(s * env / 1.6);
}

// Low "thump": a sine that drops in pitch, gone in a few tenths of a second. Body under the chime.
float sub(double t, double f) {
    double env = t < 0.003 ? t / 0.003 : std::exp(-t * 14.0);
    double ph = kTwoPi * f * (t - 0.6 * (1 - std::exp(-t * 25.0)) / 25.0);   // starts 1.6x higher, settles
    return (float)(std::sin(ph) * env);
}

std::vector<Event> score(int style, int tier) {
    std::vector<Event> n;
    if (style == JINGLE_FANFARE) {
        // C major: C5 = 3, E5 = 7, G5 = 10, C6 = 15, E6 = 19, G6 = 22, C7 = 27
        const double step = tier >= 2 ? 0.065 : 0.075;
        std::vector<int> run = tier == 0 ? std::vector<int>{10, 15, 19} : tier == 1 ? std::vector<int>{7, 10, 15, 19}
                                                                                : std::vector<int>{3, 7, 10, 15, 19, 22};
        double t = 0;
        for (size_t i = 0; i < run.size(); i++, t += step) n.push_back({BELL, t, step * 1.6, run[i], 0.55f, i % 2 ? 0.35f : -0.35f});
        double hold = tier == 0 ? 0.4 : tier == 1 ? 0.5 : tier == 2 ? 0.65 : 0.8;
        std::vector<int> chord = tier >= 2 ? std::vector<int>{15, 19, 22, 27} : std::vector<int>{15, 19, 22};
        for (size_t i = 0; i < chord.size(); i++) n.push_back({BELL, t, hold, chord[i], 0.42f, (float)i / chord.size() - 0.4f});
        if (tier >= 2) {   // sparkle on top
            n.push_back({BELL, t + 0.12, 0.25, 31, 0.22f, 0.6f});
            n.push_back({BELL, t + 0.22, 0.25, 34, 0.18f, -0.6f});
            if (tier == 3) n.push_back({BELL, t + 0.32, 0.4, 39, 0.16f, 0.2f});
        }
        return n;
    }
    // chime: whoosh (0 .. 0.2 s) rising into the strike at 0.17 s
    const double hit = 0.17;
    const double ring = tier == 0 ? 0.55 : tier == 1 ? 0.7 : tier == 2 ? 0.85 : 1.0;
    n.push_back({WHOOSH, 0.0, hit + 0.06, 0, tier >= 2 ? 0.65f : 0.55f, 0.f});
    n.push_back({SUB, hit, 0.35, -29, 0.55f, 0.f});                                  // E2 thump
    // two-note chime, up a fifth: D#6 then A#6 (a bright, "unlocked" interval), each with its octave below
    n.push_back({CHIME, hit, ring, 18, 0.55f, -0.25f});
    n.push_back({CHIME, hit, ring, 6, 0.30f, -0.1f});
    n.push_back({CHIME, hit + 0.085, ring, 25, 0.55f, 0.25f});
    n.push_back({CHIME, hit + 0.085, ring, 13, 0.28f, 0.1f});
    if (tier >= 1) n.push_back({CHIME, hit + 0.17, ring * 0.8, 30, 0.22f, 0.5f});     // sparkle: D#7
    if (tier >= 2) n.push_back({CHIME, hit + 0.25, ring * 0.7, 37, 0.17f, -0.5f});    // A#7
    if (tier == 3) {                                                                     // platinum: a third strike
        n.push_back({CHIME, hit + 0.34, ring * 0.8, 42, 0.15f, 0.3f});                  // D#8
        n.push_back({SUB, hit + 0.34, 0.3, -17, 0.25f, 0.f});
    }
    return n;
}

double scoreLength(const std::vector<Event>& n) {
    double end = 0;
    for (const Event& x : n) end = std::fmax(end, x.start + x.len);
    return end + 0.35;   // release + echo tail
}

int clampTier(int t) { return t < 0 ? 0 : t > 3 ? 3 : t; }
}  // namespace

int jingleStyleOf(const std::string& name) { return name == "fanfare" ? JINGLE_FANFARE : JINGLE_CHIME; }

size_t jingleFrames(int style, int tier) { return (size_t)(scoreLength(score(style, clampTier(tier))) * JINGLE_RATE); }

void jingleSynth(int style, int tier, int volume, int16_t* out) {
    tier = clampTier(tier);
    std::vector<Event> notes = score(style, tier);
    const size_t frames = jingleFrames(style, tier);
    const float vol = (float)(volume < 0 ? 0 : volume > 100 ? 100 : volume) / 100.f;
    // Rendered straight into `out` (the module's heap is small: no float copy). Pass 1 mixes the events at a fixed
    // gain with headroom, pass 2 adds the echoes, pass 3 scales to 85 % of full range x volume.
    float sumAmp = 0;
    for (const Event& n : notes) sumAmp += n.amp;
    const float pre = 9000.f / (sumAmp * 0.6f);
    memset(out, 0, frames * 2 * sizeof(int16_t));
    auto clip = [](float v) -> int16_t { return (int16_t)(v > 32767.f ? 32767 : v < -32768.f ? -32768 : std::lround(v)); };
    for (const Event& n : notes) {
        const double f = freqOf(n.semi);
        const size_t from = (size_t)(n.start * JINGLE_RATE);
        const size_t to = std::min(frames, (size_t)((n.start + n.len + 0.6) * JINGLE_RATE));
        uint32_t seed = 0x2545F491u;   // whoosh noise: fixed seed, same sound every time
        float lp = 0, lp2 = 0;
        for (size_t i = from; i < to; i++) {
            const double t = (double)(i - from) / JINGLE_RATE;
            float s = 0, pan = n.pan;
            switch (n.kind) {
                case BELL: s = bell(t, n.len, f); break;
                case CHIME: s = chime(t, n.len, f); break;
                case SUB: s = sub(t, f); break;
                case WHOOSH: {
                    if (t > n.len) break;
                    seed = seed * 1664525u + 1013904223u;
                    float noise = (float)((int32_t)seed) / 2147483648.f;
                    double x = t / n.len;                                    // 0 .. 1
                    double fc = 250.0 * std::pow(28.0, x);                     // band rises 250 Hz -> 7 kHz
                    float a = (float)(1.0 - std::exp(-kTwoPi * fc / JINGLE_RATE));
                    float b = (float)(1.0 - std::exp(-kTwoPi * fc * 0.35 / JINGLE_RATE));
                    lp += a * (noise - lp);
                    lp2 += b * (noise - lp2);
                    double env = std::pow(x, 1.6) * (x > 0.85 ? (1 - x) / 0.15 : 1.0);   // swell, quick cut at the strike
                    s = (float)((lp - lp2) * env * 7.0);
                    pan = (float)(x * 1.2 - 0.6);                                   // sweeps left -> right
                    break;
                }
            }
            const float gl = n.amp * (1.f - pan) * 0.5f * pre, gr = n.amp * (1.f + pan) * 0.5f * pre;
            out[2 * i] = clip(out[2 * i] + s * gl);
            out[2 * i + 1] = clip(out[2 * i + 1] + s * gr);
        }
    }
    // Echoes: the fanfare gets a ping-pong; the chime a short "room" (two close cross taps) and a longer soft one.
    struct Tap { double delay; float gain; };
    std::vector<Tap> taps = style == JINGLE_FANFARE ? std::vector<Tap>{{0.11, 0.28f}}
                                                    : std::vector<Tap>{{0.031, 0.22f}, {0.067, 0.18f}, {0.19, 0.16f}};
    for (const Tap& tp : taps) {
        const size_t d = (size_t)(tp.delay * JINGLE_RATE);
        for (size_t i = d; i < frames; i++) {
            out[2 * i] = clip(out[2 * i] + tp.gain * out[2 * (i - d) + 1]);
            out[2 * i + 1] = clip(out[2 * i + 1] + tp.gain * out[2 * (i - d)]);
        }
    }
    int peak = 1;
    for (size_t i = 0; i < 2 * frames; i++) peak = std::max(peak, std::abs((int)out[i]));
    const float g = 0.85f * 32767.f / peak * vol;
    const size_t fade = (size_t)(0.03 * JINGLE_RATE);   // no click at the end
    for (size_t i = 0; i < frames; i++) {
        float k = i + fade > frames ? (float)(frames - i) / fade : 1.f;
        out[2 * i] = clip(out[2 * i] * g * k);
        out[2 * i + 1] = clip(out[2 * i + 1] * g * k);
    }
}

// ---------------------------------------------------------------- WAV
static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

bool wavDecode(const uint8_t* d, size_t size, int volume, size_t maxFrames, std::vector<int16_t>& out, std::string& err) {
    out.clear();
    if (size < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) { err = "not a WAV file"; return false; }
    int fmt = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* pcm = nullptr;
    size_t pcmSize = 0;
    for (size_t off = 12; off + 8 <= size;) {
        uint32_t len = rd32(d + off + 4);
        const uint8_t* body = d + off + 8;
        size_t avail = size - off - 8;
        if (!memcmp(d + off, "fmt ", 4) && len >= 16 && avail >= 16) {
            fmt = rd16(body); ch = rd16(body + 2); rate = rd32(body + 4); bits = rd16(body + 14);
        } else if (!memcmp(d + off, "data", 4)) {
            pcm = body;
            pcmSize = len < avail ? len : avail;
        }
        if (len > avail) break;
        off += 8 + len + (len & 1);
    }
    if (fmt != 1 && fmt != 0xFFFE) { err = "only uncompressed PCM WAVs are supported"; return false; }
    if ((ch != 1 && ch != 2) || (bits != 8 && bits != 16) || rate < 8000 || rate > 192000) { err = "WAV must be 8/16-bit, mono or stereo"; return false; }
    if (!pcm || !pcmSize) { err = "WAV has no sound data"; return false; }
    const size_t bytesPer = (size_t)ch * (bits / 8);
    const size_t inFrames = pcmSize / bytesPer;
    auto sample = [&](size_t frame, int c) -> float {
        const uint8_t* p = pcm + frame * bytesPer + (size_t)c * (bits / 8);
        return bits == 8 ? (p[0] - 128) / 128.f : (int16_t)rd16(p) / 32768.f;
    };
    size_t frames = (size_t)((double)inFrames * JINGLE_RATE / rate);
    if (frames > maxFrames) frames = maxFrames;
    if (!frames) { err = "WAV is empty"; return false; }
    const float vol = (float)(volume < 0 ? 0 : volume > 100 ? 100 : volume) / 100.f * 32767.f;
    out.resize(frames * 2);
    for (size_t i = 0; i < frames; i++) {   // linear resampling to 48 kHz
        double pos = (double)i * rate / JINGLE_RATE;
        size_t a = (size_t)pos;
        size_t b = a + 1 < inFrames ? a + 1 : a;
        float k = (float)(pos - a);
        for (int c = 0; c < 2; c++) {
            int src = ch == 2 ? c : 0;
            float v = sample(a, src) * (1 - k) + sample(b, src) * k;
            out[2 * i + c] = (int16_t)std::lround(std::fmax(-1.f, std::fmin(1.f, v)) * vol);
        }
    }
    return true;
}
