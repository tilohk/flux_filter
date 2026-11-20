#pragma once

#include "FluxLookAndFeel.h"
#include "PluginProcessor.h"

#include <juce_dsp/juce_dsp.h>

namespace flux::ui
{

// Live picture of the field: every cell as a resonance peak at its current frequency.
// Peak width follows the cell's Q, height its level (tilt, flux level, matrix level).
// Left channel teal, right channel magenta; where they overlap the image is stereo-identical.
// Behind the cells: the spectrum of the filtered output (grey).
class FieldView : public juce::Component, private juce::Timer
{
public:
    explicit FieldView (FluxFilterAudioProcessor& p) : processor (p)
    {
        window.resize (fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (colours::viewport);
        g.fillRoundedRectangle (r, 8.0f);

        const auto plot = r.reduced (12.0f, 10.0f).withTrimmedBottom (14.0f);
        drawGrid (g, plot);
        drawSpectrum (g, plot);

        const auto& snap = processor.getFieldSnapshot();
        const int cells = juce::jlimit (0, FluxField::maxCells, snap.activeCells.load (std::memory_order_relaxed));
        const juce::Colour chColour[2] = { colours::channelL, colours::channelR };

        for (int ch = 1; ch >= 0; --ch)   // right first, left on top
            for (int c = 0; c < cells; ++c)
            {
                const float f = snap.freq[(size_t) ch][(size_t) c].load (std::memory_order_relaxed);
                const float q = snap.q[(size_t) ch][(size_t) c].load (std::memory_order_relaxed);
                const float level = snap.level[(size_t) ch][(size_t) c].load (std::memory_order_relaxed);
                if (f <= 0.0f || level <= 0.001f)
                    continue;
                drawPeak (g, plot, f, q, level, chColour[ch]);
            }

        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (cells) + " cells", r.reduced (12.0f, 6.0f).toNearestInt(), juce::Justification::topRight);
    }

private:
    static constexpr float minHz = 20.0f, maxHz = 20000.0f;

    static float xFor (const juce::Rectangle<float>& plot, float hz)
    {
        const float t = std::log (hz / minHz) / std::log (maxHz / minHz);
        return plot.getX() + t * plot.getWidth();
    }

    void drawGrid (juce::Graphics& g, const juce::Rectangle<float>& plot)
    {
        g.setFont (juce::FontOptions (10.0f));
        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xFor (plot, hz);
            const bool major = hz == 100.0f || hz == 1000.0f || hz == 10000.0f;
            g.setColour (colours::viewportGrid.withAlpha (major ? 1.0f : 0.5f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            if (major || hz == 500.0f || hz == 5000.0f || hz == 50.0f)
            {
                g.setColour (colours::dim);
                const juce::String txt = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
                g.drawText (txt, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 1.0f, 40.0f, 12.0f), juce::Justification::centred);
            }
        }
    }

    // A cell as a resonance peak that fades into the background: the curve reaches exactly zero at
    // its edges (no steps at the bottom), the fill is a gradient to transparent, only the top is stroked.
    static void drawPeak (juce::Graphics& g, const juce::Rectangle<float>& plot, float hz, float q, float level, juce::Colour colour)
    {
        // Width in octaves: a resonance's -3 dB bandwidth is about 1.44/Q octaves; widened a bit to stay visible.
        const float bwOct = juce::jlimit (0.025f, 2.0f, 1.2f / juce::jmax (q, 0.1f));
        const float height = juce::jlimit (0.0f, 1.0f, level / 1.35f) * plot.getHeight() * 0.8f;   // ~2/3 of the height at unity
        const float centreOct = std::log2 (hz);
        constexpr float reach = 6.0f;                           // half-width in bandwidths
        constexpr float tail = 1.0f / (1.0f + reach * reach);   // subtracted so the edges land on zero

        juce::Path curve;
        constexpr int steps = 48;
        bool started = false;
        float firstX = 0.0f, lastX = 0.0f;
        for (int i = 0; i <= steps; ++i)
        {
            const float o = centreOct + bwOct * reach * ((float) i / (float) steps * 2.0f - 1.0f);
            const float hzAt = std::exp2 (o);
            if (hzAt < minHz || hzAt > maxHz) continue;
            const float d = (o - centreOct) / bwOct;
            const float shape = juce::jmax (0.0f, (1.0f / (1.0f + d * d) - tail) / (1.0f - tail));
            const juce::Point<float> pt (xFor (plot, hzAt), plot.getBottom() - height * shape);
            if (! started) { curve.startNewSubPath (pt); firstX = pt.x; started = true; }
            else           curve.lineTo (pt);
            lastX = pt.x;
        }
        if (! started) return;

        juce::Path fill (curve);
        fill.lineTo (lastX, plot.getBottom());
        fill.lineTo (firstX, plot.getBottom());
        fill.closeSubPath();

        const float top = plot.getBottom() - height;
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.35f), 0.0f, top, colour.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        g.fillPath (fill);
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.95f), 0.0f, top, colour.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        g.strokePath (curve, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Output spectrum: 1/6-octave smoothed, tilted +3 dB/oct around 1 kHz (so typical music reads
    // flat), eased over time, drawn as a soft gradient with a glowing outline.
    void drawSpectrum (juce::Graphics& g, const juce::Rectangle<float>& plot)
    {
        if (! hasSignal) return;
        // The range follows the signal: its loudest band sits near the top, the floor 54 dB below.
        const float floorDb = rangeTop - rangeDb;
        auto yFor = [&] (float db) { return plot.getBottom() - plot.getHeight() * 0.95f * juce::jlimit (0.0f, 1.0f, (db - floorDb) / rangeDb); };

        juce::Path line;
        juce::Point<float> prev;
        for (int i = 0; i < numPoints; ++i)
        {
            const juce::Point<float> pt (xFor (plot, pointHz[(size_t) i]), yFor (smoothDb[(size_t) i]));
            if (i == 0) line.startNewSubPath (pt);
            else        line.quadraticTo (prev, (prev + pt) * 0.5f);   // midpoint smoothing
            prev = pt;
        }
        line.lineTo (prev);

        juce::Path fill (line);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();

        const auto tint = colours::spectrum;
        g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.20f), 0.0f, plot.getY() + plot.getHeight() * 0.2f,
                                                 tint.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        g.fillPath (fill);
        g.setColour (tint.withAlpha (0.08f));
        g.strokePath (line, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (tint.withAlpha (0.42f));
        g.strokePath (line, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void updateSpectrum()
    {
        std::array<float, 4096> incoming;
        int n;
        while ((n = processor.pullAnalyserSamples (incoming.data(), (int) incoming.size())) > 0)
            for (int i = 0; i < n; ++i)
            {
                history[(size_t) writePos] = incoming[(size_t) i];
                writePos = (writePos + 1) % fftSize;
            }

        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = history[(size_t) ((writePos + i) % fftSize)] * window[(size_t) i];
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        // Power per bin, scaled so a full-scale sine reads about 0 dB (Hann window, one side).
        const float norm = 4.0f / (float) fftSize;
        for (int b = 0; b < numBins; ++b)
        {
            const float m = fftData[(size_t) b] * norm;
            power[(size_t) b] = m * m;
        }

        const float sr = processor.getSampleRateForUi();
        const float binHz = sr / (float) fftSize;
        const float bandRatio = std::exp2 (1.0f / 12.0f);   // ±1/12 octave = 1/6-octave bands
        hasSignal = false;
        float loudest = -200.0f;
        for (int i = 0; i < numPoints; ++i)
        {
            const float hz = pointHz[(size_t) i];
            const float lo = hz / bandRatio / binHz, hi = hz * bandRatio / binHz;
            float p;
            if (hi - lo < 1.0f)   // narrower than a bin (lows): interpolate
            {
                const float x = juce::jlimit (0.0f, (float) numBins - 2.0f, hz / binHz);
                const int b = (int) x;
                p = power[(size_t) b] + (power[(size_t) b + 1] - power[(size_t) b]) * (x - (float) b);
            }
            else
            {
                const int b0 = juce::jlimit (0, numBins - 1, (int) std::ceil (lo)), b1 = juce::jlimit (0, numBins - 1, (int) std::floor (hi));
                double sum = 0.0;
                for (int b = b0; b <= b1; ++b) sum += power[(size_t) b];
                p = (float) (sum / (double) juce::jmax (1, b1 - b0 + 1));
            }
            const float db = 10.0f * std::log10 (p + 1.0e-14f) + 3.0f * std::log2 (hz / 1000.0f);
            auto& d = smoothDb[(size_t) i];
            d += (db - d) * (db > d ? 0.45f : 0.12f);   // fast rise, slow fall
            loudest = juce::jmax (loudest, d);
            hasSignal |= d > silenceDb;
        }
        if (hasSignal)
        {
            const float targetTop = juce::jlimit (-60.0f, 6.0f, loudest + 2.0f);
            rangeTop += (targetTop - rangeTop) * (targetTop > rangeTop ? 0.3f : 0.015f);
        }
    }

    void timerCallback() override
    {
        updateSpectrum();
        repaint();
    }

    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder, numBins = fftSize / 2, numPoints = 240;
    static constexpr float rangeDb = 54.0f, silenceDb = -90.0f;
    float rangeTop = -24.0f;   // auto range: rises quickly with louder material, falls back slowly
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window;
    std::vector<float> history = std::vector<float> (fftSize, 0.0f), fftData = std::vector<float> (fftSize * 2, 0.0f),
                       power = std::vector<float> (numBins, 0.0f);
    std::array<float, numPoints> pointHz = [] {
        std::array<float, numPoints> a {};
        for (int i = 0; i < numPoints; ++i) a[(size_t) i] = minHz * std::pow (maxHz / minHz, (float) i / (float) (numPoints - 1));
        return a;
    }();
    std::array<float, numPoints> smoothDb = [] { std::array<float, numPoints> a {}; a.fill (-120.0f); return a; }();
    int writePos = 0;
    bool hasSignal = false;

    FluxFilterAudioProcessor& processor;
};

} // namespace flux::ui
