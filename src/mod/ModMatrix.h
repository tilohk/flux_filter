#pragma once

#include "ModSources.h"
#include "dsp/FluxAbsorber.h"
#include "params/ParamSpec.h"

#include <array>
#include <atomic>
#include <cmath>

namespace flux
{

enum class ModSource : int { None = 0, Lfo1, Lfo2, Count };
inline constexpr const char* modSourceNames[] = { "None", "LFO 1", "LFO 2" };

// Targets. The first three after None act per cell, so an LFO's Flux amount can let the living
// cells absorb them, each in its own way. Everything else is a global parameter.
enum class ModTarget : int
{
    None = 0,
    Vowel, Resonance, Scatter,                                 // per cell
    Shift, Stretch, Feedback, Damping,
    FluxAmount, Wander, Rasp,
    Drive, Mix, Output,
    Lfo1Rate, Lfo2Rate,
    Count
};
inline constexpr const char* modTargetNames[] = {
    "None", "Vowel (cells)", "Resonance (cells)", "Scatter (cells)",
    "Shift", "Stretch", "Feedback", "Damping",
    "Flux", "Wander", "Rasp",
    "Drive", "Mix", "Output",
    "LFO 1 Rate", "LFO 2 Rate"
};

inline constexpr int numCellTargets = 3;   // Vowel, Resonance, Scatter
inline constexpr bool isCellTarget (ModTarget t) noexcept { return t >= ModTarget::Vowel && t <= ModTarget::Scatter; }
inline constexpr bool isLfo (ModSource s) noexcept { return s >= ModSource::Lfo1 && s <= ModSource::Lfo2; }

// The parameter (knob) a target modulates.
inline spec::FP targetParam (ModTarget t) noexcept
{
    using spec::FP;
    switch (t)
    {
        case ModTarget::Vowel:      return FP::Vowel;
        case ModTarget::Resonance:  return FP::Resonance;
        case ModTarget::Scatter:    return FP::Scatter;
        case ModTarget::Shift:      return FP::Shift;
        case ModTarget::Stretch:    return FP::Stretch;
        case ModTarget::Feedback:   return FP::Feedback;
        case ModTarget::Damping:    return FP::Damping;
        case ModTarget::FluxAmount: return FP::FluxAmount;
        case ModTarget::Wander:     return FP::Wander;
        case ModTarget::Rasp:       return FP::Rasp;
        case ModTarget::Drive:      return FP::Drive;
        case ModTarget::Mix:        return FP::Mix;
        case ModTarget::Output:     return FP::Output;
        case ModTarget::Lfo1Rate:   return FP::Lfo1Rate;
        case ModTarget::Lfo2Rate:   return FP::Lfo2Rate;
        case ModTarget::None:
        case ModTarget::Count:      return FP::Count;
    }
    return FP::Count;
}

// Inverse of targetParam: which target sits on a knob (None if the knob can't be modulated).
inline ModTarget targetForParam (spec::FP p) noexcept
{
    for (int t = 1; t < (int) ModTarget::Count; ++t)
        if (targetParam ((ModTarget) t) == p)
            return (ModTarget) t;
    return ModTarget::None;
}

// One connection: source → target with a bipolar depth in the target's normalised (knob) range.
struct SlotSettings
{
    ModSource source = ModSource::None;
    ModTarget target = ModTarget::None;
    float depth = 0.0f;
};

struct ModSettings
{
    static constexpr int numLfos = 2, numSlots = 16;
    std::array<LfoSettings, numLfos> lfo {};
    std::array<SlotSettings, numSlots> slots {};
};

// Mod sources + connections, evaluated once per control tick by the field.
//   tickSources()  – advance every source
//   computeSlots() – sum all connections into global offsets, per-cell offsets and flux inputs
//   absorb()       – let the living cells take up the flux inputs, each in its own way
template <int MaxCells>
class ModMatrix
{
public:
    void prepare (float sampleRate, float ticksPerSecond)
    {
        sr = sampleRate;
        for (int i = 0; i < ModSettings::numLfos; ++i)
            lfos[(size_t) i].reset (0x1F123BB5u + 977u * (uint32_t) i);
        for (auto& a : absorbers) a.prepare (ticksPerSecond);
        values.fill (0.0f);
        globalOffset.fill (0.0f);
        cellDirect.fill (0.0f);
        fluxInput.fill (0.0f);
    }

    void setSettings (const ModSettings& s) noexcept { settings = s; }

    // Once per audio block, before processing it.
    void setTransport (const Transport& t) noexcept { blockTransport = t; }

    // sampleOffset: position of this tick inside the current block (for the tempo-synced LFOs).
    void tickSources (float dt, int sampleOffset) noexcept
    {
        Transport t = blockTransport;
        t.ppq += (double) sampleOffset / (double) sr * t.bpm / 60.0;

        auto rate = [this] (spec::FP p, float base, ModTarget tgt) {
            return modulated (p, base, globalOffset[(size_t) tgt]);   // last tick's offsets
        };

        values[(size_t) ModSource::None] = 0.0f;

        const spec::FP rateParams[] = { spec::FP::Lfo1Rate, spec::FP::Lfo2Rate };
        const ModTarget rateTargets[] = { ModTarget::Lfo1Rate, ModTarget::Lfo2Rate };
        for (int i = 0; i < ModSettings::numLfos; ++i)
        {
            const auto& ls = settings.lfo[(size_t) i];
            const float hz = rate (rateParams[i], ls.rateHz, rateTargets[i]);
            values[(size_t) ModSource::Lfo1 + (size_t) i] = lfos[(size_t) i].advance (ls, hz, t, dt);
        }

        for (size_t i = 0; i < values.size(); ++i)
            published[i].store (values[i], std::memory_order_relaxed);
    }

    // For the UI: the latest value of a source, readable from any thread.
    float publishedSource (ModSource s) const noexcept { return published[(size_t) s].load (std::memory_order_relaxed); }

    void computeSlots() noexcept
    {
        globalOffset.fill (0.0f);
        cellDirect.fill (0.0f);
        fluxInput.fill (0.0f);

        for (const auto& slot : settings.slots)
        {
            if (slot.source == ModSource::None || slot.target == ModTarget::None || slot.depth == 0.0f)
                continue;

            const float amount = slot.depth * source (slot.source);
            if (isCellTarget (slot.target))
            {
                // The LFO's Flux amount decides how much of the movement the living cells absorb.
                const auto k = (size_t) slot.target - (size_t) ModTarget::Vowel;
                const float f = fluxShare (slot.source);
                fluxInput[k] += amount * f;
                cellDirect[k] += amount * (1.0f - f);
            }
            else
            {
                globalOffset[(size_t) slot.target] += amount;
            }
        }
    }

    float fluxShare (ModSource s) const noexcept
    {
        return isLfo (s) ? std::clamp (settings.lfo[(size_t) s - (size_t) ModSource::Lfo1].flux, 0.0f, 1.0f) : 0.0f;
    }

    using Weights = typename FluxAbsorber<MaxCells>::Weights;

    // weight: each cell's life gain; scatter: the field's Flux (0 = the cells follow in lockstep).
    void absorb (const Weights& weight, int numCells, float scatter = 1.0f) noexcept
    {
        for (int k = 0; k < numCellTargets; ++k)
            absorbers[(size_t) k].process (fluxInput[(size_t) k], weight, numCells, scatter);
    }

    // Normalised offset for a global target.
    float global (ModTarget t) const noexcept { return globalOffset[(size_t) t]; }

    // Normalised offset for a per-cell target (direct connections + what this cell absorbed).
    float cell (ModTarget t, int c) const noexcept
    {
        const auto k = (size_t) t - (size_t) ModTarget::Vowel;
        return cellDirect[k] + absorbers[k].get (c);
    }

    float source (ModSource s) const noexcept { return values[(size_t) s]; }

    // base (natural units) + offset (normalised) → natural units, clamped to the parameter's range.
    static float modulated (spec::FP p, float base, float offset) noexcept
    {
        if (offset == 0.0f || p == spec::FP::Count)
            return base;
        const auto& s = spec::get (p);
        return s.from01 (s.to01 (base) + offset);
    }

private:
    float sr = 48000.0f;
    ModSettings settings;
    Transport blockTransport;

    std::array<ModLfo, ModSettings::numLfos> lfos;

    std::array<float, (size_t) ModSource::Count> values {};
    std::array<std::atomic<float>, (size_t) ModSource::Count> published {};
    std::array<float, (size_t) ModTarget::Count> globalOffset {};
    std::array<float, numCellTargets> cellDirect {}, fluxInput {};
    std::array<FluxAbsorber<MaxCells>, numCellTargets> absorbers;
};

static_assert (sizeof (modSourceNames) / sizeof (modSourceNames[0]) == (size_t) ModSource::Count);
static_assert (sizeof (modTargetNames) / sizeof (modTargetNames[0]) == (size_t) ModTarget::Count);

} // namespace flux
