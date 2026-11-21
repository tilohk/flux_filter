#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Throat shapes for the formant field: each is up to five formants (frequency, bandwidth, level).
// The Vowel knob walks a ring through them: a → e → i → o → u → nasal → growl → schwa → whisper → a.
// Neighbouring shapes are interpolated in log frequency / log bandwidth / dB.
//
// The vowels follow a classic tenor formant set (narrow, singing bandwidths); Resonance scales all
// bandwidths, so these values are what Resonance 60 % sounds like. The last four are designed, not
// measured: nasal (low F1, weak muffled upper formants), growl (low, close F1/F2, thick),
// schwa (a neutral tube: evenly spaced formants), whisper (F1 almost gone, energy up high, wide).
namespace flux::formants
{

inline constexpr int numFormants = 5;

struct Formant
{
    float hz, bwHz, gainDb;
};

struct Shape
{
    const char* name;
    std::array<Formant, numFormants> f;
};

inline constexpr std::array<Shape, 9> shapes {{
    { "a",       {{ { 650, 80, 0 },   { 1080, 90, -6 },   { 2650, 120, -7 },  { 2900, 130, -8 },  { 3250, 140, -22 } }} },
    { "e",       {{ { 400, 70, 0 },   { 1700, 80, -14 },  { 2600, 100, -12 }, { 3200, 120, -14 }, { 3580, 120, -20 } }} },
    { "i",       {{ { 290, 40, 0 },   { 1870, 90, -15 },  { 2800, 100, -18 }, { 3250, 120, -20 }, { 3540, 120, -30 } }} },
    { "o",       {{ { 400, 40, 0 },   { 800, 80, -10 },   { 2600, 100, -12 }, { 2800, 120, -12 }, { 3000, 120, -26 } }} },
    { "u",       {{ { 350, 40, 0 },   { 600, 60, -20 },   { 2700, 100, -17 }, { 2900, 120, -14 }, { 3300, 120, -26 } }} },
    { "nasal",   {{ { 250, 60, 0 },   { 1000, 200, -18 }, { 2200, 150, -16 }, { 2900, 200, -20 }, { 3500, 250, -28 } }} },
    { "growl",   {{ { 300, 120, 0 },  { 550, 140, -2 },   { 1500, 200, -14 }, { 2400, 250, -20 }, { 3300, 300, -28 } }} },
    { "schwa",   {{ { 500, 80, 0 },   { 1500, 100, -6 },  { 2500, 120, -12 }, { 3500, 150, -18 }, { 4500, 180, -24 } }} },
    { "whisper", {{ { 700, 300, -30 },{ 1800, 250, -8 },  { 3200, 300, 0 },   { 4600, 400, -2 },  { 6500, 500, -4 } }} },
}};

inline constexpr int numShapes = (int) shapes.size();

// Ring position (any real number) wrapped into [0, numShapes).
inline float wrap (float pos) noexcept
{
    pos = std::fmod (pos, (float) numShapes);
    return pos < 0.0f ? pos + (float) numShapes : pos;
}

// Formant k (0 = F1) of the shape at ring position pos.
inline Formant at (float pos, int k) noexcept
{
    pos = wrap (pos);
    const int i = std::min ((int) pos, numShapes - 1);
    const float t = pos - (float) i;
    const auto& a = shapes[(size_t) i].f[(size_t) k];
    const auto& b = shapes[(size_t) ((i + 1) % numShapes)].f[(size_t) k];
    return { a.hz * std::pow (b.hz / a.hz, t),
             a.bwHz * std::pow (b.bwHz / a.bwHz, t),
             a.gainDb + (b.gainDb - a.gainDb) * t };
}

// How many formants a field of n cells forms: 1–3 cells: 1, 4–6: 2, 7–9: 3, 10–12: 4, 13–16: 5.
inline int countFor (int cells) noexcept { return std::clamp ((cells + 2) / 3, 1, numFormants); }

} // namespace flux::formants
