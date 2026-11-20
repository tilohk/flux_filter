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
        for (int scale = 0; scale < 4; ++scale)
            for (float combine : { 0.0f, 0.5f, 1.0f })
            {
                FluxField f; FieldSettings s;
                s.density = 24; s.resonance = 1.0f; s.driveDb = 24.0f; s.combine = combine;
                s.scale = (CellScale) scale; s.tiltDbOct = 12.0f;
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
                std::printf ("stress scale=%d combine=%.1f  peakL=%.2f peakR=%.2f\n", scale, combine, a.peak, b.peak);
                CHECK (a.finite && b.finite, "non-finite output (scale %d, combine %.1f)", scale, combine);
                CHECK (a.peak < 16.0f && b.peak < 16.0f, "runaway level %.2f/%.2f (scale %d, combine %.1f)", a.peak, b.peak, scale, combine);
            }
    }

    // 2. Resonant peaks sit on the cells: a sine on a cell passes louder than one between cells.
    {
        FieldSettings s; s.density = 8; s.centerHz = 1000.0f; s.spread = 4.0f; s.resonance = 0.85f;
        s.morph = 1.0f; s.width = 0.0f;
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

        FluxField g; g.setSettings (FieldSettings {}); g.prepare (sr);
        auto w = noise (48000 * 2, 0.5f); const double inRms = rmsTail (w); run (g, w, nullptr);
        const double ratioDb = 20.0 * std::log10 (rmsTail (w) / inRms);
        std::printf ("default settings on noise: %.1f dB vs input\n", ratioDb);
        CHECK (ratioDb > -24.0 && ratioDb < 12.0, "default level off: %.1f dB", ratioDb);
    }

    std::printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
