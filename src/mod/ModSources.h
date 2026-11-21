#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace flux
{

inline float bipolarRandom (uint32_t& state) noexcept
{
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return (float) (state >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

// ---------------------------------------------------------------------------------------------
// Analog-style LFO: a fixed set of shapes, free-running in Hz or locked to the host grid.
// Bipolar (-1..1) or unipolar (0..1).
// ---------------------------------------------------------------------------------------------
enum class ModLfoShape { Sine = 0, Triangle, Saw, Square, Random };

inline constexpr const char* lfoShapeNames[] = { "Sine", "Triangle", "Saw", "Square", "Random" };
inline constexpr int numLfoShapes = 5;

// Tempo divisions: name and length in quarter notes.
struct Division { const char* name; double beats; };
// Sorted from long to short, so turning the knob steps through them in order.
inline constexpr Division lfoDivisions[] = {
    { "4 bars", 16.0 }, { "2 bars", 8.0 }, { "1 bar", 4.0 },
    { "1/2 D", 3.0 }, { "1/2", 2.0 }, { "1/2 T", 4.0 / 3.0 },
    { "1/4 D", 1.5 }, { "1/4", 1.0 }, { "1/4 T", 2.0 / 3.0 },
    { "1/8 D", 0.75 }, { "1/8", 0.5 }, { "1/8 T", 1.0 / 3.0 },
    { "1/16 D", 0.375 }, { "1/16", 0.25 }, { "1/16 T", 1.0 / 6.0 },
    { "1/32", 0.125 },
};
inline constexpr int defaultLfoDivision = 7;   // 1/4
inline constexpr int numLfoDivisions = (int) (sizeof (lfoDivisions) / sizeof (lfoDivisions[0]));

struct LfoSettings
{
    float rateHz = 0.5f;
    bool  sync = false;
    int   division = defaultLfoDivision;   // index into lfoDivisions
    bool  unipolar = false;      // 0..1 instead of -1..1
    ModLfoShape shape = ModLfoShape::Sine;
    float flux = 0.0f;           // 0..1, share of the cell modulation the living cells absorb
};

struct Transport
{
    bool   playing = false;
    double bpm = 120.0;
    double ppq = 0.0;            // position at the current tick, in quarter notes
};

class ModLfo
{
public:
    void reset (uint32_t seed) noexcept
    {
        phase = 0.0;
        rng = seed;
        prevRandom = bipolarRandom (rng);
        nextRandom = bipolarRandom (rng);
    }

    float advance (const LfoSettings& s, float rateHz, const Transport& t, float dt) noexcept
    {
        const double beats = lfoDivisions[std::clamp (s.division, 0, numLfoDivisions - 1)].beats;
        double newPhase;
        if (s.sync && t.playing)
            newPhase = t.ppq / beats;                                  // locked to the grid
        else if (s.sync)
            newPhase = phase + (t.bpm / 60.0 / beats) * (double) dt;   // stopped: run at tempo
        else
            newPhase = phase + (double) rateHz * (double) dt;

        newPhase -= std::floor (newPhase);
        if (newPhase < phase - 0.5 || (s.sync && t.playing && std::abs (newPhase - phase) > 0.5))
            nextCycle();
        phase = newPhase;
        const float v = shapeAt (s.shape, (float) phase);
        return s.unipolar ? 0.5f * (v + 1.0f) : v;
    }

private:
    void nextCycle() noexcept
    {
        prevRandom = nextRandom;
        nextRandom = bipolarRandom (rng);
    }

    float shapeAt (ModLfoShape shape, float p) const noexcept
    {
        switch (shape)
        {
            case ModLfoShape::Triangle: return 1.0f - 4.0f * std::abs (p - 0.5f);
            case ModLfoShape::Saw:      return 2.0f * p - 1.0f;   // rising ramp, then drop
            case ModLfoShape::Square:   return p < 0.5f ? 1.0f : -1.0f;
            case ModLfoShape::Random:
            {
                const float t = 0.5f - 0.5f * std::cos (p * 3.14159265f);
                return prevRandom + (nextRandom - prevRandom) * t;
            }
            case ModLfoShape::Sine:
            default:                    return std::sin (6.28318530718f * p);
        }
    }

    double phase = 0.0;
    uint32_t rng = 1;
    float prevRandom = 0.0f, nextRandom = 0.0f;
};

} // namespace flux
