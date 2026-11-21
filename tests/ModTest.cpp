// Offline tests for the mod sources, the matrix and their effect on the field (no JUCE needed).
// Build & run:  cmake --build <build-dir> --target FluxModTest && <build-dir>/FluxModTest
#include "dsp/FluxField.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace flux;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr float sr = 48000.0f;
static constexpr int tickLen = FluxField::controlInterval;
static constexpr float ticksPerSec = sr / (float) tickLen;
static constexpr float dt = 1.0f / ticksPerSec;

using Matrix = ModMatrix<24>;

// Runs the matrix alone for `seconds`.
template <typename OnTick>
static void runMatrix (Matrix& m, float seconds, OnTick onTick, const Matrix::Weights* weights = nullptr, float scatter = 1.0f, int cells = 12)
{
    const int ticks = (int) (seconds * ticksPerSec);
    for (int k = 0; k < ticks; ++k)
    {
        m.tickSources (dt, 0);
        m.computeSlots();
        static const Matrix::Weights allAlive = [] { Matrix::Weights w {}; w.fill (1.0f); return w; }();
        m.absorb (weights != nullptr ? *weights : allAlive, cells, scatter);
        onTick ((float) k * dt);
    }
}

static int countRisingZeroCrossings (const std::vector<float>& v)
{
    int n = 0;
    for (size_t i = 1; i < v.size(); ++i) if (v[i - 1] < 0.0f && v[i] >= 0.0f) ++n;
    return n;
}

int main()
{
    // 1. Parameter normalisation matches the knob: centre value sits at 0.5, round trip is exact.
    {
        const auto& c = spec::get (spec::FP::Lfo1Rate);
        CHECK (std::abs (c.to01 (1.0f) - 0.5f) < 1e-4f, "rate skew: 1 Hz at %.4f", c.to01 (1.0f));
        float worst = 0.0f;
        for (float v : { 0.01f, 0.1f, 1.0f, 5.0f, 30.0f })
            worst = std::max (worst, std::abs (c.from01 (c.to01 (v)) - v) / v);
        CHECK (worst < 1e-4f, "rate round trip error %.6f", worst);
        std::printf ("param spec: LFO rate 1 Hz → %.3f, round-trip error %.2e\n", c.to01 (1.0f), worst);
    }

    // 2. LFO: rate, polarity, tempo sync locked to the host position.
    {
        ModLfo lfo; lfo.reset (1);
        LfoSettings s; s.rateHz = 1.0f;
        Transport t;
        std::vector<float> v;
        for (int k = 0; k < (int) (10 * ticksPerSec); ++k) v.push_back (lfo.advance (s, s.rateHz, t, dt));
        const int cycles = countRisingZeroCrossings (v);
        std::printf ("lfo 1 Hz: %d cycles in 10 s\n", cycles);
        CHECK (cycles >= 9 && cycles <= 10, "lfo 1 Hz gave %d cycles in 10 s", cycles);

        for (int shape = 0; shape < numLfoShapes; ++shape)
        {
            LfoSettings sh; sh.shape = (ModLfoShape) shape; ModLfo l; l.reset (9);
            float lo = 9, hi = -9;
            for (int k = 0; k < 6000; ++k) { const float x = l.advance (sh, 1.0f, t, dt); lo = std::min (lo, x); hi = std::max (hi, x); }
            CHECK (lo >= -1.0f && hi <= 1.0f && hi - lo > 0.5f, "shape %d range %.3f..%.3f", shape, lo, hi);
        }

        LfoSettings sy; sy.sync = true; sy.division = defaultLfoDivision;   // 1/4
        Transport play; play.playing = true; play.bpm = 120.0;
        ModLfo l2; l2.reset (2);
        play.ppq = 0.25; const float atQuarter = l2.advance (sy, 0.0f, play, dt);
        play.ppq = 3.0;  const float atBeat = l2.advance (sy, 0.0f, play, dt);
        std::printf ("lfo sync 1/4: value %.3f a quarter into the beat, %.3f on the beat\n", atQuarter, atBeat);
        CHECK (std::abs (atQuarter - 1.0f) < 1e-3f && std::abs (atBeat) < 1e-3f, "sync not locked to ppq (%.3f, %.3f)", atQuarter, atBeat);
    }

    // 5. Unipolar LFOs stay in 0..1 and keep the shape.
    {
        ModLfo l; l.reset (4);
        LfoSettings s; s.unipolar = true; Transport t;
        float lo = 9, hi = -9;
        for (int k = 0; k < 3000; ++k) { const float x = l.advance (s, 1.0f, t, dt); lo = std::min (lo, x); hi = std::max (hi, x); }
        std::printf ("unipolar lfo range %.3f..%.3f\n", lo, hi);
        CHECK (lo >= 0.0f && lo < 0.01f && hi <= 1.0f && hi > 0.99f, "unipolar range %.3f..%.3f", lo, hi);
    }

    // 6. Connections: depths add up, offsets clamp to the knob range.
    {
        Matrix m; m.prepare (sr, ticksPerSec);
        ModSettings s;
        s.lfo[0].rateHz = 0.7f;
        s.slots[0] = { ModSource::Lfo1, ModTarget::Mix, 0.5f };
        s.slots[1] = { ModSource::Lfo1, ModTarget::Mix, 0.25f };
        s.slots[2] = { ModSource::Lfo1, ModTarget::Shift, -1.0f };
        m.setSettings (s);
        runMatrix (m, 0.3f, [] (float) {});
        const float lfo = m.source (ModSource::Lfo1);
        std::printf ("connections: lfo %.3f → mix offset %.3f, shift offset %.3f\n", lfo, m.global (ModTarget::Mix), m.global (ModTarget::Shift));
        CHECK (std::abs (m.global (ModTarget::Mix) - 0.75f * lfo) < 1e-6f, "mix offset %.3f", m.global (ModTarget::Mix));
        CHECK (std::abs (m.global (ModTarget::Shift) + lfo) < 1e-6f, "shift offset %.3f", m.global (ModTarget::Shift));
        CHECK (std::abs (Matrix::modulated (spec::FP::Mix, 0.8f, 0.5f) - 1.0f) < 1e-6f, "modulated value must clamp to range");
        CHECK (targetForParam (spec::FP::Vowel) == ModTarget::Vowel && targetForParam (spec::FP::Mix) == ModTarget::Mix, "knob → target map");
    }

    // 7. The LFO's Flux amount: without it every cell moves exactly with the LFO; with it the
    //    living cells absorb the movement, each with its own delay (up to 0.3 s) and depth; a dead
    //    cell (life gain 0) absorbs nothing. At field Flux 0 the living cells absorb it in lockstep.
    for (int mode = 0; mode < 3; ++mode)   // 0: LFO flux off, 1: on at field Flux 1, 2: on at field Flux 0
    {
        const bool flux = mode > 0;
        const float fieldFlux = mode == 2 ? 0.0f : 1.0f;
        Matrix m; m.prepare (sr, ticksPerSec);
        ModSettings s;
        s.lfo[0].rateHz = 0.5f; s.lfo[0].flux = flux ? 1.0f : 0.0f;
        s.slots[0] = { ModSource::Lfo1, ModTarget::Vowel, 0.3f };
        m.setSettings (s);
        Matrix::Weights w {}; w.fill (1.0f);
        w[5] = 0.0f;   // cell 5 is dead
        std::vector<std::vector<float>> trace (12);
        runMatrix (m, 6.0f, [&] (float) { for (int c = 0; c < 12; ++c) trace[(size_t) c].push_back (m.cell (ModTarget::Vowel, c)); }, &w, fieldFlux);

        auto lagOf = [&] (const std::vector<float>& a, const std::vector<float>& b) {   // how much later b follows a
            int best = 0; double bestC = -1e9;
            for (int lag = -(int) (0.5f * ticksPerSec); lag <= (int) (0.5f * ticksPerSec); ++lag)
            {
                double c = 0;
                for (size_t i = a.size() / 2; i < a.size(); ++i) c += a[(size_t) ((long) i - lag)] * b[i];
                if (c > bestC) { bestC = c; best = lag; }
            }
            return (float) best / ticksPerSec;
        };
        float lo = 1e9f, hi = -1e9f, depthLo = 1e9f, depthHi = 0.0f;
        for (int c = 0; c < 12; ++c)
        {
            if (c == 5) continue;
            const float l = lagOf (trace[0], trace[(size_t) c]);
            lo = std::min (lo, l); hi = std::max (hi, l);
            const auto [mn, mx] = std::minmax_element (trace[(size_t) c].begin() + (long) trace[(size_t) c].size() / 2, trace[(size_t) c].end());
            depthLo = std::min (depthLo, *mx - *mn); depthHi = std::max (depthHi, *mx - *mn);
        }
        const auto [deadLo, deadHi] = std::minmax_element (trace[5].begin(), trace[5].end());
        std::printf ("matrix LFO → vowel, LFO flux %s, field Flux %.0f: cells lag between %.3f and %.3f s, swing %.3f..%.3f; dead cell swing %.3f\n",
                     flux ? "on " : "off", fieldFlux, lo, hi, depthLo, depthHi, *deadHi - *deadLo);
        if (mode == 2)
        {
            CHECK (hi - lo < 1e-6f && depthHi - depthLo < 1e-5f, "at field Flux 0 the living cells should move in lockstep");
            CHECK (*deadHi - *deadLo < 1e-6f, "a dead cell absorbs nothing (%.4f)", *deadHi - *deadLo);
        }
        else if (flux)
        {
            // Delays up to 0.3 s, plus the phase lag of each cell's sluggishness (up to 120 ms).
            CHECK (hi - lo > 0.1f && hi - lo < 0.45f, "absorbed lags should spread over a few hundred ms (%.3f)", hi - lo);
            CHECK (depthHi > depthLo * 1.3f, "absorbed depths should differ (%.3f..%.3f)", depthLo, depthHi);
            CHECK (*deadHi - *deadLo < 1e-6f, "a dead cell absorbs nothing (%.4f)", *deadHi - *deadLo);
        }
        else
        {
            CHECK (hi - lo < 1e-6f && depthHi - depthLo < 1e-5f, "without flux every cell moves the same");
        }
    }

    // 8. In the field: an LFO can turn the effect off via Mix; LFO → vowel moves the cells.
    {
        FieldSettings fs; fs.mix = 1.0f; fs.limiter = false; fs.fluxAmount = 0.0f;
        ModSettings ms; ms.lfo[0].shape = ModLfoShape::Square; ms.lfo[0].rateHz = 0.25f;   // +1 for the first 2 s
        ms.slots[0] = { ModSource::Lfo1, ModTarget::Mix, -1.0f };
        FluxField f; f.setSettings (fs); f.setModSettings (ms); f.prepare (sr);
        std::mt19937 rng (3); std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        std::vector<float> in (48000), out; for (auto& x : in) x = u (rng);
        out = in; for (size_t i = 0; i < out.size(); i += 256) f.process (out.data() + i, nullptr, (int) std::min<size_t> (256, out.size() - i));
        float diff = 0.0f; for (size_t i = 24000; i < in.size(); ++i) diff = std::max (diff, std::abs (out[i] - in[i]));
        std::printf ("field: LFO → mix -1 leaves dry signal, max diff %.2e\n", diff);
        CHECK (diff < 1e-5f, "LFO to mix -1 should give dry (diff %g)", diff);

        ModSettings lm; lm.lfo[0].rateHz = 1.0f;
        lm.slots[0] = { ModSource::Lfo1, ModTarget::Vowel, 0.2f };
        FluxField g; g.setSettings (fs); g.setModSettings (lm); g.prepare (sr);
        float lo = 1e9f, hi = 0.0f; std::vector<float> buf (tickLen);
        for (int k = 0; k < (int) (2 * ticksPerSec); ++k)
        {
            std::fill (buf.begin(), buf.end(), 0.0f); g.process (buf.data(), nullptr, tickLen);
            lo = std::min (lo, g.getCellFrequencyNow (5)); hi = std::max (hi, g.getCellFrequencyNow (5));
        }
        std::printf ("field: LFO → vowel 0.2 moves cell 5 over %.2f octaves\n", std::log2 (hi / lo));
        CHECK (std::log2 (hi / lo) > 0.4f, "LFO → vowel barely moves the cell (%.2f oct)", std::log2 (hi / lo));
    }

    // 9. Stress: every slot busy with random routings at full depth, all flux, extreme field.
    {
        std::mt19937 rng (11);
        float worst = 0.0f; bool finite = true;
        for (int run = 0; run < 8; ++run)
        {
            ModSettings ms;
            for (auto& sl : ms.slots)
                sl = { (ModSource) (1 + (int) (rng() % ((int) ModSource::Count - 1))),
                       (ModTarget) (1 + (int) (rng() % ((int) ModTarget::Count - 1))),
                       (rng() % 2 ? 1.0f : -1.0f) };
            for (auto& l : ms.lfo) { l.rateHz = 20.0f; l.shape = (ModLfoShape) (rng() % numLfoShapes); l.flux = 1.0f; l.unipolar = rng() % 2; }
            FieldSettings fs; fs.density = 24; fs.resonance = 1.0f; fs.feedback = 1.0f; fs.character = (Character) (run % 3);
            FluxField f; f.setSettings (fs); f.setModSettings (ms); f.prepare (sr);
            std::uniform_real_distribution<float> u (-1.0f, 1.0f);
            std::vector<float> l (48000 * 2), r (48000 * 2); for (auto& x : l) x = u (rng); for (auto& x : r) x = u (rng);
            for (size_t i = 0; i < l.size(); i += 128) f.process (l.data() + i, r.data() + i, 128);
            for (float x : l) { finite &= std::isfinite (x); worst = std::max (worst, std::abs (x)); }
            for (float x : r) { finite &= std::isfinite (x); worst = std::max (worst, std::abs (x)); }
        }
        std::printf ("stress: 8 runs with 16 random full-depth connections, all LFOs flux, peak %.3f\n", worst);
        CHECK (finite && worst <= 1.0f, "stress failed (finite %d, peak %.3f)", (int) finite, worst);
    }

    std::printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
