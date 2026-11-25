// Achievement unlock sounds: a short chime or fanfare synthesized on the fly (original, no audio file shipped) or the
// player's own WAV (sdmc:/switch/TrophyNX/sounds/unlock.wav). Output: 48 kHz, stereo, interleaved int16 - what
// the console's audio output takes. Portable: shared by the live module and the host tests.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

static const int JINGLE_RATE = 48000;
static const double JINGLE_MAX_SECONDS = 3.0;   // custom WAVs are cut here (the module's heap is small)

enum JingleStyle { JINGLE_CHIME = 0, JINGLE_FANFARE = 1 };
// "chime" / "fanfare" (popup.json "sound") -> style; anything else is the chime.
int jingleStyleOf(const std::string& name);
// Length in samples per channel of a built-in sound for a tier (0 bronze .. 3 platinum).
size_t jingleFrames(int style, int tier);
// Writes it at `volume` (0..100) into out (2 * jingleFrames(style, tier) values).
void jingleSynth(int style, int tier, int volume, int16_t* out);

// Decodes a PCM WAV (8/16-bit, mono/stereo, any rate) into 48 kHz stereo at `volume`, cut at maxFrames.
// False with err set when the file isn't a PCM WAV.
bool wavDecode(const uint8_t* data, size_t size, int volume, size_t maxFrames, std::vector<int16_t>& out, std::string& err);
