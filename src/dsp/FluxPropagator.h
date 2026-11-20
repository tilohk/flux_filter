#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace flux
{

enum class FluxDirection { Up = 0, Down, Outward, Inward };

struct PropagationSettings
{
    float travelSec  = 0.8f;   // time for the wave to cross the whole field (0 = all cells at once)
    FluxDirection direction = FluxDirection::Up;
    float viscosity  = 0.2f;   // 0..1, lag that grows along the path → the wave smears as it travels
    float wavelength = 0.0f;   // 0 = all cells move the same way; n = n spatial cycles across the field
    float turbulence = 0.2f;   // 0..1, every cell gets its own slowly changing arrival time and depth
    float stereo     = 0.3f;   // 0..1, the right channel hears the wave later (up to 250 ms)
};

// Turns one control signal into a value per cell: the signal enters at one end of the field and
// travels through it like a wave through water.
//   source → delay line (control rate) → per-cell read position (path position × travel time
//   + turbulence + stereo offset) → per-cell spatial weight (wavelength, turbulence)
//   → per-cell viscosity lag.
// Everything runs once per control tick; no per-sample work.
template <int MaxCells, int MaxChannels = 2>
class FluxPropagator
{
public:
    static constexpr float maxTravelSec = 4.0f;
    static constexpr float maxStereoSec = 0.25f;
    static constexpr float maxTurbulenceSec = 0.5f;

    // Allocates; call from prepareToPlay, not the audio thread.
    void prepare (float ticksPerSecond) noexcept
    {
        tickRate = ticksPerSecond;
        tickDt = 1.0f / ticksPerSecond;
        const float maxDelaySec = maxTravelSec + maxStereoSec + maxTurbulenceSec + 0.1f;
        history.assign ((size_t) std::ceil (maxDelaySec * ticksPerSecond) + 4, 0.0f);
        writePos = 0;
        rng = 0x7F4A7C15u;
        for (auto& v : seedDelay) v = nextBipolar();
        for (auto& v : seedDepth) v = nextBipolar();
        wander.fill (0.0f);
        for (auto& ch : smoothed) ch.fill (0.0f);
        primed = false;
    }

    // Push this tick's source value, then compute every cell's value.
    void process (float source, const PropagationSettings& s, int numCells) noexcept
    {
        if (history.empty())
            return;

        if (! primed)   // start settled: no wave rushing in from silence at load time
        {
            std::fill (history.begin(), history.end(), source);
            for (auto& ch : smoothed) ch.fill (source);
            primed = true;
        }

        writePos = (writePos + 1) % history.size();
        history[writePos] = source;

        updateWander();

        const float visTau = 2.0f * s.viscosity * s.viscosity;   // 0..2 s, fine control at the low end
        numCells = std::clamp (numCells, 1, MaxCells);

        for (int c = 0; c < numCells; ++c)
        {
            const float x = pathPosition (c, numCells, s.direction);
            const float turbA = s.turbulence * (0.6f * seedDelay[(size_t) c] + 0.4f * wander[(size_t) c]);

            float delaySec = x * s.travelSec + std::max (0.0f, turbA) * maxTurbulenceSec;

            float weight = s.wavelength > 0.0f ? std::cos (6.28318530718f * s.wavelength * x) : 1.0f;
            weight *= 1.0f + 0.5f * s.turbulence * seedDepth[(size_t) c];

            // Viscosity: cells further along the path are more sluggish.
            const float tau = visTau * (0.25f + 0.75f * x);
            const float coef = tau > 1.0e-4f ? 1.0f - std::exp (-tickDt / tau) : 1.0f;

            for (int ch = 0; ch < MaxChannels; ++ch)
            {
                const float d = delaySec + (ch == 1 ? s.stereo * maxStereoSec : 0.0f);
                const float raw = read (d * tickRate) * weight;
                auto& y = smoothed[(size_t) ch][(size_t) c];
                y += (raw - y) * coef;
                values[(size_t) ch][(size_t) c] = y;
            }
        }
    }

    float get (int cell, int ch) const noexcept { return values[(size_t) ch][(size_t) cell]; }

    // 0 at the point where the wave enters, 1 where it leaves.
    static float pathPosition (int c, int numCells, FluxDirection dir) noexcept
    {
        const float u = numCells > 1 ? (float) c / (float) (numCells - 1) : 0.0f;
        switch (dir)
        {
            case FluxDirection::Down:    return 1.0f - u;
            case FluxDirection::Outward: return std::abs (u - 0.5f) * 2.0f;
            case FluxDirection::Inward:  return 1.0f - std::abs (u - 0.5f) * 2.0f;
            case FluxDirection::Up:
            default:                     return u;
        }
    }

private:
    // Linear interpolation, delay in ticks.
    float read (float delayTicks) const noexcept
    {
        const float maxD = (float) history.size() - 2.0f;
        delayTicks = std::clamp (delayTicks, 0.0f, maxD);
        const auto whole = (size_t) delayTicks;
        const float frac = delayTicks - (float) whole;
        const size_t n = history.size();
        const size_t i0 = (writePos + n - whole) % n;
        const size_t i1 = (i0 + n - 1) % n;
        return history[i0] + (history[i1] - history[i0]) * frac;
    }

    // Slow independent wander per cell (~0.1 Hz) so turbulence keeps changing.
    void updateWander() noexcept
    {
        constexpr float theta = 6.28318530718f * 0.1f;
        const float sigma = std::sqrt (2.0f * theta * tickDt);
        for (auto& w : wander)
            w += -theta * w * tickDt + sigma * (nextBipolar() + nextBipolar() + nextBipolar());
    }

    float nextBipolar() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    float tickRate = 1500.0f, tickDt = 1.0f / 1500.0f;
    std::vector<float> history;
    size_t writePos = 0;
    bool primed = false;
    uint32_t rng = 0x7F4A7C15u;

    std::array<float, MaxCells> seedDelay {}, seedDepth {}, wander {};
    std::array<std::array<float, MaxCells>, MaxChannels> smoothed {}, values {};
};

} // namespace flux
