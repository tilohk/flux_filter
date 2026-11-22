#pragma once

#include "FluxLookAndFeel.h"
#include "PluginProcessor.h"

#include <juce_dsp/juce_dsp.h>

namespace flux::ui
{

// Live picture of the field: one line, the filter's combined response, which is what the input
// actually goes through: every living cell summed (with their phases), or in cut mode every
// cell's notch multiplied, then the feedback loop, the makeup / auto gain and the dry/wet mix.
// Boost fills under the line (peaks rise from below); cut shades what is removed, hanging from 0 dB. As cells drift, are born and die, the line breathes.
// Its colour follows Flux (including modulation): sage at 0, the flux amber at 100.
// Behind it: the spectrum of the filtered output (grey).
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
        drawResponse (g, plot);
    }

private:
    static constexpr float minHz = 20.0f, maxHz = 20000.0f;
    static constexpr float topDb = 15.0f, bottomDb = -45.0f;   // response scale; 0 dB at 75 % of the height

    static float yForDb (const juce::Rectangle<float>& plot, float db)
    {
        return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db - bottomDb) / (topDb - bottomDb));
    }

    // Recomputed every frame from the snapshot (both channels are identical).
    void updateResponse()
    {
        const auto& snap = processor.getFieldSnapshot();
        constexpr auto relaxed = std::memory_order_relaxed;
        const int cells = juce::jlimit (0, FluxField::maxCells, snap.activeCells.load (relaxed));
        std::array<float, FluxField::maxCells> freq {}, q {}, level {};
        for (int c = 0; c < cells; ++c)
        {
            freq[(size_t) c] = juce::jmax (1.0f, snap.freq[(size_t) c].load (relaxed));
            q[(size_t) c] = juce::jmax (0.1f, snap.q[(size_t) c].load (relaxed));
            level[(size_t) c] = snap.level[(size_t) c].load (relaxed);
        }
        const float norm = snap.norm.load (relaxed), post = snap.post.load (relaxed);
        const float fb = snap.feedback.load (relaxed), mix = snap.mix.load (relaxed);
        const float sr = processor.getSampleRateForUi();
        const float dampCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * snap.dampHz.load (relaxed) / sr);
        hasResponse = cells > 0;
        cutMode = snap.cut.load (relaxed);
        const float flux = juce::jlimit (0.0f, 1.0f, snap.flux.load (relaxed));
        fluxLevel += (flux - fluxLevel) * 0.2f;

        std::array<float, curvePoints> rawPower {};
        for (int i = 0; i < curvePoints; ++i)
        {
            const float hz = curveHz[(size_t) i];
            const auto lv = [&] (int c) { return level[(size_t) c]; };
            const auto h = cutMode ? FluxField::cutResponse (hz, cells, freq.data(), q.data(), lv)
                                   : norm * FluxField::response (hz, cells, freq.data(), q.data(), lv);
            // Feedback: the wet signal goes back in through the damping lowpass (one pole):
            // wet = H x / (1 - fb D H). Near self-oscillation the saturation holds it; cap the display at +20 dB.
            std::complex<float> wet = h;
            if (fb > 0.0f && hz < 0.5f * sr)
            {
                const auto z1 = std::polar (1.0f, -juce::MathConstants<float>::twoPi * hz / sr);
                const auto damp = dampCoef / (1.0f - (1.0f - dampCoef) * z1);
                auto denom = 1.0f - fb * damp * h;
                if (std::abs (denom) < 0.1f) denom *= 0.1f / std::abs (denom);
                wet = h / denom;
            }
            const auto total = (1.0f - mix) + mix * post * wet;
            rawPower[(size_t) i] = std::norm (total);
        }

        // Round the tips: average over about 1/5 octave (a small bell), so narrow resonances read
        // as soft hills rather than needles. Then ease over time.
        constexpr int half = 4;
        constexpr float bell[2 * half + 1] = { 0.03f, 0.08f, 0.15f, 0.2f, 0.24f, 0.2f, 0.15f, 0.08f, 0.03f };
        for (int i = 0; i < curvePoints; ++i)
        {
            // Cut mode averages in dB instead: averaged power would fill the notches in.
            float p = 0.0f, w = 0.0f;
            for (int k = -half; k <= half; ++k)
            {
                const int j = juce::jlimit (0, curvePoints - 1, i + k);
                const float raw = rawPower[(size_t) j];
                p += bell[k + half] * (cutMode ? 10.0f * std::log10 (raw + 1.0e-12f) : raw);
                w += bell[k + half];
            }
            const float db = cutMode ? p / w : 10.0f * std::log10 (p / w + 1.0e-12f);
            auto& d = responseDb[(size_t) i];
            d += (db - d) * 0.6f;   // a touch of easing so the 30 Hz frames don't flicker
        }
    }

    void drawResponse (juce::Graphics& g, const juce::Rectangle<float>& plot)
    {
        // 0 dB reference.
        g.setColour (colours::viewportGrid.withAlpha (0.7f));
        const float zeroY = yForDb (plot, 0.0f);
        const float dashes[] = { 3.0f, 4.0f };
        g.drawDashedLine ({ plot.getX(), zeroY, plot.getRight(), zeroY }, dashes, 2, 1.0f);
        if (! hasResponse) return;

        juce::Path line;
        std::array<juce::Point<float>, curvePoints> pts;
        for (int i = 0; i < curvePoints; ++i)
        {
            pts[(size_t) i] = { xFor (plot, curveHz[(size_t) i]), yForDb (plot, responseDb[(size_t) i]) };
            if (i == 0) line.startNewSubPath (pts[0]);
            else        line.quadraticTo (pts[(size_t) i - 1], (pts[(size_t) i - 1] + pts[(size_t) i]) * 0.5f);   // midpoint smoothing
        }
        line.lineTo (pts.back());

        const auto colour = colours::channelL.interpolatedWith (colours::flux, fluxLevel);
        juce::Path fill (line);
        if (cutMode)
        {
            // What the cells take away: between the 0 dB line and the curve, strongest at the top.
            fill.lineTo (plot.getRight(), zeroY);
            fill.lineTo (plot.getX(), zeroY);
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.28f), 0.0f, zeroY, colour.withAlpha (0.04f), 0.0f, plot.getBottom(), false));
        }
        else
        {
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.lineTo (plot.getX(), plot.getBottom());
            fill.closeSubPath();
            const float top = yForDb (plot, topDb);
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.30f), 0.0f, top, colour.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        }
        g.fillPath (fill);

        g.setColour (colour.withAlpha (0.95f));
        g.strokePath (line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

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
        updateResponse();
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
    static constexpr int curvePoints = 400;   // ~40 per octave
    std::array<float, curvePoints> curveHz = [] {
        std::array<float, curvePoints> a {};
        for (int i = 0; i < curvePoints; ++i) a[(size_t) i] = minHz * std::pow (maxHz / minHz, (float) i / (float) (curvePoints - 1));
        return a;
    }();
    std::array<float, curvePoints> responseDb = [] { std::array<float, curvePoints> a {}; a.fill (bottomDb); return a; }();
    float fluxLevel = 0.0f;   // eased, for the line's colour
    int writePos = 0;
    bool hasSignal = false, hasResponse = false, cutMode = false;

    FluxFilterAudioProcessor& processor;
};

} // namespace flux::ui
