// Offline sanity tests for the flux field DSP (no JUCE needed).
// Build & run:  cmake --build <build-dir> --target FluxFieldTest && <build-dir>/FluxFieldTest
#include "dsp/FluxField.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace flux;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double sr = 48000.0;

struct Result { float peak = 0.0f; double rms = 0.0; bool finite = true; };

static Result run (FluxField& f, std::vector<float>& l, std::vector<float>* r, int block = 256)
{
    for (size_t i = 0; i < l.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, l.size() - i);
        f.process (l.data() + i, r ? r->data() + i : nullptr, n);
    }
    Result res; double sum = 0.0;
    for (float v : l) { res.finite &= std::isfinite (v); res.peak = std::max (res.peak, std::abs (v)); sum += (double) v * v; }
    res.rms = std::sqrt (sum / (double) l.size());
    return res;
}

static std::vector<float> noise (size_t n, float amp, unsigned seed = 1)
{
    std::mt19937 rng (seed); std::uniform_real_distribution<float> d (-amp, amp);
    std::vector<float> v (n); for (auto& x : v) x = d (rng); return v;
}

static std::vector<float> sine (size_t n, float hz, float amp)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i) v[i] = amp * (float) std::sin (2.0 * M_PI * hz * (double) i / sr);
    return v;
}

static double rmsTail (const std::vector<float>& v) // skip the attack
{
    double s = 0.0; const size_t start = v.size() / 2;
    for (size_t i = start; i < v.size(); ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) (v.size() - start));
}

int main()
{
    // 1. Stress: extreme settings, loud noise, settings jumping every block.
    {
        std::mt19937 rng (7); std::uniform_real_distribution<float> u (0.0f, 1.0f);
        float worstUnlimited = 0.0f;
        for (int scale = 0; scale < 4; ++scale)
            for (float combine : { 0.0f, 0.5f, 1.0f })
            for (int character = 0; character < 3; ++character)
            for (bool limiter : { true, false })
            {
                FluxField f; FieldSettings s;
                s.density = 24; s.resonance = 1.0f; s.driveDb = 24.0f; s.combine = combine;
                s.scale = (CellScale) scale; s.tiltDbOct = 12.0f;
                s.character = (Character) character; s.limiter = limiter;
                s.feedback = 1.0f; s.drift = 1.0f; s.damping = 0.0f;
                s.fluxDepth = 3.0f; s.fluxLevel = 1.0f; s.fluxRateHz = 20.0f; s.fluxShape = LfoShape::Random;
                s.propagation.turbulence = 1.0f; s.propagation.wavelength = 4.0f; s.propagation.stereo = 1.0f;
                f.setSettings (s); f.prepare (sr);

                auto l = noise ((size_t) sr * 2, 1.0f, 1), r = noise ((size_t) sr * 2, 1.0f, 2);
                for (size_t i = 0; i < l.size(); i += 64)
                {
                    s.centerHz = 20.0f + 11980.0f * u (rng) * u (rng);
                    s.morph = 3.0f * u (rng);
                    s.spread = 8.0f * u (rng);
                    f.setSettings (s);
                    f.process (l.data() + i, r.data() + i, 64);
                }
                Result a, b; double dummy = 0;
                for (float v : l) { a.finite &= std::isfinite (v); a.peak = std::max (a.peak, std::abs (v)); }
                for (float v : r) { b.finite &= std::isfinite (v); b.peak = std::max (b.peak, std::abs (v)); }
                (void) dummy;
                const float peak = std::max (a.peak, b.peak);
                worstUnlimited = limiter ? worstUnlimited : std::max (worstUnlimited, peak);
                CHECK (a.finite && b.finite, "non-finite output (scale %d, combine %.1f, char %d)", scale, combine, character);
                if (limiter)
                    CHECK (peak <= 1.0f, "limiter let %.3f through (scale %d, combine %.1f, char %d)", peak, scale, combine, character);
                else
                    CHECK (peak <= 4.001f, "wet ceiling broken, level %.2f (scale %d, combine %.1f, char %d)", peak, scale, combine, character);
            }
        std::printf ("stress: 72 extreme combinations finite, limiter holds; worst peak without limiter %.2f\n", worstUnlimited);
    }

    // 2. Resonant peaks sit on the cells: a sine on a cell passes louder than one between cells.
    {
        FieldSettings s; s.density = 8; s.centerHz = 1000.0f; s.spread = 4.0f; s.resonance = 0.85f;
        s.morph = 1.0f; s.width = 0.0f; s.drift = 0.0f; s.autoGain = false; s.fluxDepth = 0.0f;
        FluxField probe; probe.setSettings (s); probe.prepare (sr);
        std::vector<float> tmp (64, 0.0f); probe.process (tmp.data(), nullptr, 64);
        const float onCell = probe.getCellFrequency (3);
        const float between = std::sqrt (probe.getCellFrequency (3) * probe.getCellFrequency (4));

        auto measure = [&] (float hz) {
            FluxField f; f.setSettings (s); f.prepare (sr);
            auto v = sine ((size_t) sr, hz, 0.1f); run (f, v, nullptr); return rmsTail (v);
        };
        const double on = measure (onCell), off = measure (between);
        std::printf ("peaks: on-cell %.0f Hz rms=%.4f, between %.0f Hz rms=%.4f\n", onCell, on, between, off);
        CHECK (on > off * 2.0, "cell peak not pronounced (%.4f vs %.4f)", on, off);
    }

    // 3. Harmonic scale places cells on integer multiples of the root.
    {
        FieldSettings s; s.scale = CellScale::Harmonic; s.centerHz = 110.0f; s.spread = 4.0f; s.density = 6;
        FluxField f; f.setSettings (s); f.prepare (sr);
        std::vector<float> tmp (64, 0.0f); f.process (tmp.data(), nullptr, 64);
        for (int i = 0; i < 6; ++i)
            CHECK (std::abs (f.getCellFrequency (i) - 110.0f * (float) (i + 1)) < 0.01f, "harmonic cell %d at %.2f", i, f.getCellFrequency (i));
    }

    // 4. Mix = 0 is a clean bypass; default settings keep a sane level.
    {
        FieldSettings s; s.mix = 0.0f;
        FluxField f; f.setSettings (s); f.prepare (sr);
        auto in = noise (48000, 0.5f), v = in; run (f, v, nullptr);
        float maxDiff = 0.0f; for (size_t i = 0; i < v.size(); ++i) maxDiff = std::max (maxDiff, std::abs (v[i] - in[i]));
        CHECK (maxDiff < 1e-6f, "mix=0 not transparent (diff %g)", maxDiff);

        for (bool ag : { false, true })
        {
            FieldSettings d; d.autoGain = ag;
            FluxField g; g.setSettings (d); g.prepare (sr);
            auto w = noise (48000 * 3, 0.3f); const double inRms = rmsTail (w); run (g, w, nullptr);
            const double ratioDb = 20.0 * std::log10 (rmsTail (w) / inRms);
            std::printf ("default settings on noise, auto gain %s: %.1f dB vs input\n", ag ? "on " : "off", ratioDb);
            if (ag) CHECK (std::abs (ratioDb) < 3.0, "auto gain misses input level: %.1f dB", ratioDb);
            else    CHECK (ratioDb > -24.0 && ratioDb < 12.0, "default level off: %.1f dB", ratioDb);
        }
    }

    // 5. Drift: cells wander when on, stay put when off.
    {
        for (float amount : { 0.0f, 1.0f })
        {
            FieldSettings s; s.drift = amount; s.width = 0.0f; s.fluxDepth = 0.0f;
            FluxField f; f.setSettings (s); f.prepare (sr);
            std::vector<float> buf (64, 0.0f);
            float lo = 1e9f, hi = 0.0f;
            for (int b = 0; b < 48000 * 4 / 64; ++b)
            {
                std::fill (buf.begin(), buf.end(), 0.0f); f.process (buf.data(), nullptr, 64);
                lo = std::min (lo, f.getCellFrequencyNow (5)); hi = std::max (hi, f.getCellFrequencyNow (5));
            }
            const float rangeOct = std::log2 (hi / lo);
            std::printf ("drift %.0f: cell 5 moved over %.3f octaves in 4 s\n", amount, rangeOct);
            if (amount == 0.0f) CHECK (rangeOct < 1e-4f, "cells move without drift (%.4f oct)", rangeOct);
            else                CHECK (rangeOct > 0.05f && rangeOct < 1.0f, "drift range odd: %.3f oct", rangeOct);
        }
    }

    // 6. Feedback near the top self-oscillates (sustains after the input stops), stays bounded,
    //    and auto gain does not pump the tail up.
    {
        FieldSettings s; s.feedback = 1.0f; s.damping = 0.0f; s.resonance = 0.9f; s.scale = CellScale::Harmonic;
        s.centerHz = 110.0f;
        FluxField f; f.setSettings (s); f.prepare (sr);
        auto v = noise (48000, 0.3f);                      // 1 s of input...
        v.resize (48000 * 4, 0.0f);                        // ...then 3 s of silence
        run (f, v, nullptr);
        double tail = 0.0; for (size_t i = 48000 * 3; i < v.size(); ++i) tail += (double) v[i] * v[i];
        tail = std::sqrt (tail / 48000.0);
        float peak = 0.0f; for (float x : v) peak = std::max (peak, std::abs (x));
        std::printf ("feedback 1.0: tail rms %.4f after 2 s of silence, peak %.3f, auto gain %.2f\n", tail, peak, f.getAutoGain());
        CHECK (tail > 1e-3, "no self-oscillation at feedback 1.0 (tail %.6f)", tail);
        CHECK (peak <= 1.0f, "feedback peak %.3f", peak);

        FieldSettings q = s; q.feedback = 0.0f;
        FluxField g; g.setSettings (q); g.prepare (sr);
        auto w = noise (48000, 0.3f); w.resize (48000 * 4, 0.0f); run (g, w, nullptr);
        double tail0 = 0.0; for (size_t i = 48000 * 3; i < w.size(); ++i) tail0 += (double) w[i] * w[i];
        CHECK (tail0 < 1e-6, "tail without feedback should die out (%.8f)", tail0);
    }

    // 7. Flux propagation: the wave crosses the field in "travel" seconds, in the chosen direction.
    {
        constexpr int ticksPerSec = 48000 / FluxField::controlInterval;   // 1500
        struct Trace { std::vector<float> first, last, firstR; };
        auto record = [&] (FieldSettings s, float seconds) {
            s.drift = 0.0f; s.fluxRateHz = 0.5f; s.fluxShape = LfoShape::Sine; s.density = 12;
            FluxField f; f.setSettings (s); f.prepare (sr);
            Trace t; std::vector<float> l (FluxField::controlInterval), r (FluxField::controlInterval);
            for (int k = 0; k < (int) (seconds * ticksPerSec); ++k)
            {
                std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
                f.process (l.data(), r.data(), FluxField::controlInterval);
                t.first.push_back (f.getFluxValue (0, 0));
                t.last.push_back (f.getFluxValue (11, 0));
                t.firstR.push_back (f.getFluxValue (0, 1));
            }
            return t;
        };
        // Lag (seconds) by which b follows a, searched over 0..1 s, using the second half of the trace.
        auto lagOf = [&] (const std::vector<float>& a, const std::vector<float>& b) {
            const int n = (int) a.size(), start = n / 2; int best = 0; double bestC = -1e9;
            for (int lag = 0; lag <= ticksPerSec; ++lag)
            {
                double c = 0.0;
                for (int i = start; i < n; ++i) c += (double) a[(size_t) (i - lag)] * b[(size_t) i];
                if (c > bestC) { bestC = c; best = lag; }
            }
            return (float) best / (float) ticksPerSec;
        };
        auto corr = [] (const std::vector<float>& a, const std::vector<float>& b) {
            double ab = 0, aa = 0, bb = 0;
            for (size_t i = a.size() / 2; i < a.size(); ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
            return ab / std::sqrt (aa * bb + 1e-30);
        };
        auto range = [] (const std::vector<float>& a) {
            float lo = 1e9f, hi = -1e9f;
            for (size_t i = a.size() / 2; i < a.size(); ++i) { lo = std::min (lo, a[i]); hi = std::max (hi, a[i]); }
            return hi - lo;
        };
        FieldSettings base; base.propagation = { 0.0f, FluxDirection::Up, 0.0f, 0.0f, 0.0f, 0.0f };

        auto t0 = record (base, 6.0f);
        float maxDiff = 0.0f; for (size_t i = 0; i < t0.first.size(); ++i) maxDiff = std::max (maxDiff, std::abs (t0.first[i] - t0.last[i]));
        std::printf ("flux travel 0: max difference first/last cell %.6f\n", maxDiff);
        CHECK (maxDiff < 1e-5f, "travel 0 should move all cells together (%.6f)", maxDiff);

        FieldSettings up = base; up.propagation.travelSec = 0.5f;
        auto tu = record (up, 6.0f);
        const float lagUp = lagOf (tu.first, tu.last);
        std::printf ("flux up, travel 0.5 s: last cell follows first by %.3f s\n", lagUp);
        CHECK (std::abs (lagUp - 0.5f) < 0.01f, "up lag %.3f s, expected 0.5", lagUp);

        FieldSettings down = up; down.propagation.direction = FluxDirection::Down;
        auto td = record (down, 6.0f);
        const float lagDown = lagOf (td.last, td.first);
        std::printf ("flux down, travel 0.5 s: first cell follows last by %.3f s\n", lagDown);
        CHECK (std::abs (lagDown - 0.5f) < 0.01f, "down lag %.3f s, expected 0.5", lagDown);

        FieldSettings wl = base; wl.propagation.wavelength = 0.5f;
        auto tw = record (wl, 6.0f);
        const double cw = corr (tw.first, tw.last);
        std::printf ("flux wavelength 0.5: first/last correlation %.3f (opposite motion)\n", cw);
        CHECK (cw < -0.99, "wavelength 0.5 should move ends in opposition (%.3f)", cw);

        FieldSettings vis = base; vis.propagation.viscosity = 1.0f;
        auto tv = record (vis, 8.0f);
        std::printf ("flux viscosity 1: movement range first %.3f, last %.3f\n", range (tv.first), range (tv.last));
        CHECK (range (tv.last) < range (tv.first) * 0.8f, "viscosity should smear the far end");

        FieldSettings st = base; st.propagation.stereo = 1.0f;
        auto ts = record (st, 6.0f);
        const float lagR = lagOf (ts.first, ts.firstR);
        std::printf ("flux stereo 1: right follows left by %.3f s\n", lagR);
        CHECK (std::abs (lagR - 0.25f) < 0.01f, "stereo lag %.3f s, expected 0.25", lagR);

        FieldSettings out = up; out.propagation.direction = FluxDirection::Outward;
        auto to = record (out, 6.0f);
        const float lagOut = lagOf (to.first, to.last);
        std::printf ("flux outward: both edges arrive together (lag %.3f s)\n", lagOut);
        CHECK (lagOut < 0.01f, "outward edges should be in sync (%.3f)", lagOut);
    }

    std::printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
