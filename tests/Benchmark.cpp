// CPU benchmark for the field (no JUCE): seconds of audio processed per second of CPU.
// Build & run:  cmake --build <build-dir> --target FluxBenchmark && <build-dir>/FluxBenchmark
// (Use an optimised build for meaningful numbers.)
#include "dsp/FluxField.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace flux;

static double run (double sr, int density, bool busyMatrix, bool follow = false, int block = 256, double seconds = 20.0)
{
    FieldSettings fs; fs.density = density; fs.resonance = 0.8f; fs.fluxAmount = 0.6f; fs.character = Character::Warm;
    fs.rasp = 0.5f;   // every life process on (drift, coupling, life cycle, rasp)
    fs.stereoIndependent = true;
    if (follow) fs.anchor = AnchorMode::Follow;
    ModSettings ms;
    if (busyMatrix)
    {
        for (auto& l : ms.lfo) { l.rateHz = 3.0f; l.flux = 0.7f; }
        const ModTarget targets[] = { ModTarget::Vowel, ModTarget::Resonance, ModTarget::Scatter, ModTarget::Width };
        for (int i = 0; i < ModSettings::numSlots; ++i)
            ms.slots[(size_t) i] = { (ModSource) (1 + i % 3), targets[i % 4], 0.2f };
    }
    FluxField f; f.setSettings (fs); f.setModSettings (ms); f.prepare (sr);

    std::mt19937 rng (1); std::uniform_real_distribution<float> u (-0.5f, 0.5f);
    std::vector<float> l ((size_t) block), r ((size_t) block);
    const long total = (long) (sr * seconds);
    const auto t0 = std::chrono::steady_clock::now();
    for (long done = 0; done < total; done += block)
    {
        for (int i = 0; i < block; ++i) { l[(size_t) i] = u (rng); r[(size_t) i] = u (rng); }
        f.process (l.data(), r.data(), block);
    }
    const double cpu = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    return 100.0 * cpu / seconds;   // % of one core
}

int main()
{
    std::printf ("%-34s %8s\n", "case (stereo, block 256)", "% core");
    for (double sr : { 48000.0, 96000.0 })
        for (int d : { 8, 16 })
            for (bool busy : { false, true })
                std::printf ("%5.0f kHz, %2d cells, %-16s %8.2f\n", sr / 1000.0, d, busy ? "16 connections" : "no connections", run (sr, d, busy));
    for (double sr : { 48000.0, 96000.0 })
        std::printf ("%5.0f kHz, 16 cells, 16 conn., follow %6.2f\n", sr / 1000.0, run (sr, 16, true, true));
    return 0;
}
