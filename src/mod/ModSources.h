#pragma once

#include "CurveShape.h"

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
// LFO with classic shapes plus a drawable one. Free-running in Hz, or locked to the host grid.
// ---------------------------------------------------------------------------------------------
enum class ModLfoShape { Sine = 0, Triangle, SawUp, SawDown, Square, Random, Custom };

inline constexpr const char* lfoShapeNames[] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random", "Custom" };

// Tempo divisions: name and length in quarter notes.
struct Division { const char* name; double beats; };
inline constexpr Division lfoDivisions[] = {
    { "4 bars", 16.0 }, { "2 bars", 8.0 }, { "1 bar", 4.0 }, { "1/2", 2.0 }, { "1/4", 1.0 },
    { "1/8", 0.5 }, { "1/16", 0.25 }, { "1/32", 0.125 },
    { "1/2 T", 4.0 / 3.0 }, { "1/4 T", 2.0 / 3.0 }, { "1/8 T", 1.0 / 3.0 }, { "1/16 T", 1.0 / 6.0 },
    { "1/2 D", 3.0 }, { "1/4 D", 1.5 }, { "1/8 D", 0.75 }, { "1/16 D", 0.375 },
};
inline constexpr int numLfoDivisions = (int) (sizeof (lfoDivisions) / sizeof (lfoDivisions[0]));

struct LfoSettings
{
    float rateHz = 0.5f;
    bool  sync = false;
    int   division = 4;          // index into lfoDivisions (1/4)
    ModLfoShape shape = ModLfoShape::Sine;
    float phaseOffset = 0.0f;    // 0..1
    bool  unipolar = false;
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
        CurveShape::defaultShape().bake (table);
    }

    CurveTableExchange& curveExchange() noexcept { return exchange; }

    float advance (const LfoSettings& s, float rateHz, const Transport& t, float dt) noexcept
    {
        exchange.update (table);

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

        double p = phase + (double) s.phaseOffset;
        p -= std::floor (p);
        const float v = shapeAt (s.shape, (float) p);
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
            case ModLfoShape::SawUp:    return 2.0f * p - 1.0f;
            case ModLfoShape::SawDown:  return 1.0f - 2.0f * p;
            case ModLfoShape::Square:   return p < 0.5f ? 1.0f : -1.0f;
            case ModLfoShape::Random:
            {
                const float t = 0.5f - 0.5f * std::cos ((float) phase * 3.14159265f);
                return prevRandom + (nextRandom - prevRandom) * t;
            }
            case ModLfoShape::Custom:
            {
                const float x = p * (float) CurveShape::tableSize;
                const int i = std::min ((int) x, CurveShape::tableSize - 1);
                const float f = x - (float) i;
                return table[(size_t) i] + (table[(size_t) i + 1] - table[(size_t) i]) * f;
            }
            case ModLfoShape::Sine:
            default:                    return std::sin (6.28318530718f * p);
        }
    }

    double phase = 0.0;
    uint32_t rng = 1;
    float prevRandom = 0.0f, nextRandom = 0.0f;
    CurveTableExchange::Table table {};
    CurveTableExchange exchange;
};

// ---------------------------------------------------------------------------------------------
// Envelope follower on the dry input. Three outputs, all 0..1 on a 60 dB scale:
//   Env       – follows the level with the user's attack/release
//   Body      – slow follower: the sustained part of the sound
//   Transient – how far the fast level jumps above the body: hits, plucks, consonants
// ---------------------------------------------------------------------------------------------
class EnvelopeFollower
{
public:
    void prepare (float sampleRate) noexcept
    {
        sr = sampleRate;
        env = fast = body = 0.0f;
        fastAtt = coef (0.5f); fastRel = coef (25.0f);
        bodyAtt = coef (80.0f);
    }

    // Per tick, before the samples of that tick are pushed.
    void setTimes (float attackMs, float releaseMs) noexcept
    {
        att = coef (attackMs);
        rel = coef (releaseMs);
        bodyRel = coef (std::max (releaseMs * 2.0f, 300.0f));
    }

    void push (float x) noexcept
    {
        env  += (x - env)  * (x > env  ? att : rel);
        fast += (x - fast) * (x > fast ? fastAtt : fastRel);
        body += (x - body) * (x > body ? bodyAtt : bodyRel);
    }

    void compute (float sensDb, float& outEnv, float& outBody, float& outTransient) const noexcept
    {
        outEnv  = toUnit (env, sensDb);
        outBody = toUnit (body, sensDb);
        const float jumpDb = toDb (fast) - toDb (body);
        // 3 dB threshold: steady material (fast always rides a little above body) stays silent.
        outTransient = std::clamp ((jumpDb - 3.0f) / 15.0f, 0.0f, 1.0f) * (toUnit (fast, sensDb) > 0.0f ? 1.0f : 0.0f);
    }

private:
    float coef (float ms) const noexcept { return 1.0f - std::exp (-1.0f / (std::max (ms, 0.01f) * 0.001f * sr)); }
    static float toDb (float x) noexcept { return 20.0f * std::log10 (x + 1.0e-9f); }
    static float toUnit (float x, float sensDb) noexcept { return std::clamp ((toDb (x) + sensDb + 60.0f) / 60.0f, 0.0f, 1.0f); }

    float sr = 48000.0f;
    float env = 0.0f, fast = 0.0f, body = 0.0f;
    float att = 0.1f, rel = 0.01f, fastAtt = 0.1f, fastRel = 0.01f, bodyAtt = 0.01f, bodyRel = 0.001f;
};

// ---------------------------------------------------------------------------------------------
// Lorenz attractor: never repeats, never escapes. X and Y as two bipolar sources.
// Speed 0..1 → 0.05..20× natural time; Shape 0..1 → rho 25..45 (calmer → wilder orbits).
// ---------------------------------------------------------------------------------------------
class ChaosLorenz
{
public:
    void reset() noexcept { x = 0.1f; y = 0.0f; z = 20.0f; }

    void advance (float speed, float shape, float dt) noexcept
    {
        const float timeScale = 0.05f * std::pow (400.0f, std::clamp (speed, 0.0f, 1.0f));
        const float rho = 25.0f + 20.0f * std::clamp (shape, 0.0f, 1.0f);
        constexpr float sigma = 10.0f, beta = 8.0f / 3.0f, maxStep = 0.004f;

        const float total = dt * timeScale;
        const int steps = std::max (1, (int) std::ceil (total / maxStep));
        const float h = total / (float) steps;
        for (int i = 0; i < steps; ++i)
        {
            const float dx = sigma * (y - x);
            const float dy = x * (rho - z) - y;
            const float dz = x * y - beta * z;
            x += h * dx; y += h * dy; z += h * dz;
        }
        if (! std::isfinite (x) || ! std::isfinite (y) || ! std::isfinite (z))
            reset();
    }

    float outX() const noexcept { return std::clamp (x / 20.0f, -1.0f, 1.0f); }
    float outY() const noexcept { return std::clamp (y / 27.0f, -1.0f, 1.0f); }

private:
    float x = 0.1f, y = 0.0f, z = 20.0f;
};

// ---------------------------------------------------------------------------------------------
// Random movement: Smooth (a wandering walk) or Sample & Hold (new value every cycle, with slew).
// ---------------------------------------------------------------------------------------------
class DriftSource
{
public:
    void reset (uint32_t seed) noexcept { rng = seed; walk = value = target = 0.0f; phase = 0.0f; }

    float advance (float rateHz, bool sampleHold, float slew, float dt) noexcept
    {
        if (! sampleHold)
        {
            const float theta = 6.28318530718f * rateHz;
            const float sigma = std::sqrt (2.0f * theta * dt);
            walk += -theta * walk * dt + sigma * (bipolarRandom (rng) + bipolarRandom (rng) + bipolarRandom (rng));
            walk = std::clamp (walk, -4.0f, 4.0f);
            value = std::clamp (walk / 2.2f, -1.0f, 1.0f);
            return value;
        }

        phase += rateHz * dt;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            target = bipolarRandom (rng);
        }
        const float slewSec = slew / std::max (rateHz, 0.01f);
        const float c = slewSec > 1.0e-4f ? 1.0f - std::exp (-dt / slewSec) : 1.0f;
        value += (target - value) * c;
        return value;
    }

private:
    uint32_t rng = 1;
    float walk = 0.0f, value = 0.0f, target = 0.0f, phase = 0.0f;
};

} // namespace flux
