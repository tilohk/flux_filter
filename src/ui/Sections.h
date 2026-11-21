#pragma once

#include "ParamControl.h"

namespace flux::ui
{

// A magnetic field around the big Flux knob. Five poles sit evenly along the value ring (its ends at
// 225° and 135°, both sides at ±67.5°, and the top); field lines arc between neighbouring poles and
// flow upwards: from the two bottom poles to the two middle ones and on into the top one (light
// pulses travel along the lines in that direction). The field stays close to the knob; Flux makes it
// denser, brighter, faster and more chaotic rather than bigger.
// An overlay over the whole editor panel; never takes clicks.
class FluxAura : public juce::Component, private juce::Timer
{
public:
    explicit FluxAura (juce::Slider& s) : slider (s)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    // Centre and outer radius of the knob's value ring, in this component's coordinates.
    void setRing (juce::Point<float> c, float r) { centre = c; ringR = r; repaint(); }

    void paint (juce::Graphics& g) override
    {
        if (ringR <= 0.0f) return;
        constexpr float pi = juce::MathConstants<float>::pi;
        const float v = level;
        const float R = ringR + 1.0f;
        const auto colour = colours::dim.interpolatedWith (colours::flux, juce::jlimit (0.0f, 1.0f, v * 1.8f));
        const float strength = juce::jmin (1.0f, 0.10f + 1.2f * v);

        // Between two poles Δ apart, each line rises from the ring at one pole and lands at the next:
        // r = R + (L - R) · sin (π t)^0.35, t = 0…1 along the way.
        constexpr float delta = 0.375f * pi;          // 67.5°: five poles over the ring's 270°
        const float spread = 0.16f + 0.06f * v;       // the outermost line, × R: stays close
        const int lines = 3 + juce::roundToInt (3.0f * v);
        constexpr int steps = 40;
        auto strokeBoth = [&] (const juce::Path& p, float alpha, float glowWidth) {
            if (v > 0.02f)
            {
                g.setColour (colours::flux.withAlpha (juce::jmin (1.0f, (0.12f + 0.3f * v) * alpha)));
                g.strokePath (p, juce::PathStrokeType (glowWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (colour.withAlpha (alpha));
            g.strokePath (p, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };

        for (int side : { -1, 1 })
            for (int lobe = 0; lobe < 2; ++lobe)      // 0: bottom pole → middle pole, 1: middle → top
            {
                const float fromAngle = (float) side * (0.75f * pi - delta * (float) lobe);   // angle from the top, clockwise
                for (int k = 0; k < lines; ++k)
                {
                    const float frac = (float) (k + 1) / (float) lines;
                    // Chaos: each arc (all its lines together, so they stay nested) breathes and sways,
                    // more and faster with Flux; the pulses along the lines are each their own.
                    const float arcSeed = 3.1f * (float) lobe + (side > 0 ? 0.9f : 0.0f);
                    const float seed = arcSeed + 1.7f * (float) k;
                    const float breathe = 1.0f + (0.04f + 0.18f * v) * std::sin (time * 0.9f + arcSeed);
                    const float L = R * (1.0f + spread * frac * breathe);
                    const float lineAlpha = (1.0f - 0.55f * frac) * strength;

                    std::array<juce::Point<float>, steps + 1> pts;
                    for (int i = 0; i <= steps; ++i)
                    {
                        const float t = (float) i / (float) steps;    // 0 at the lower pole, 1 at the upper one
                        const float psi = delta * t;
                        const float sway = 1.0f + (0.04f + 0.2f * v) * std::sin (pi * t)
                                                * std::sin (3.0f * pi * t - time * (1.5f + 3.0f * v) + arcSeed) * frac;
                        // A broad arc: it rises quickly from the pole and stays near its full height
                        // over most of the way (a flattened sine), so the bulges are wide, not pinched.
                        const float r = R + (L - R) * std::pow (std::sin (pi * t), 0.35f) * sway;
                        const float a = fromAngle - (float) side * psi;   // towards the top
                        pts[(size_t) i] = { centre.x + r * std::sin (a), centre.y - r * std::cos (a) };
                    }
                    juce::Path line;
                    line.startNewSubPath (pts[0]);
                    for (int i = 1; i <= steps; ++i) line.lineTo (pts[(size_t) i]);
                    strokeBoth (line, lineAlpha * 0.45f, 2.0f + 2.0f * v);

                    // Light pulses flowing upwards: positions run from the bottom pole (0) to the top (2).
                    const float speed = 0.15f + 0.6f * v;
                    for (int n = 0; n < 2; ++n)
                    {
                        const float pos = std::fmod (time * speed + 0.5f * (float) n + 0.13f * seed, 1.0f) * 2.0f - (float) lobe;
                        const float half = 0.18f;
                        const float t0 = pos - half, t1 = pos + half;
                        if (t1 <= 0.0f || t0 >= 1.0f) continue;
                        const int i0 = juce::jlimit (0, steps, (int) std::floor (t0 * (float) steps));
                        const int i1 = juce::jlimit (0, steps, (int) std::ceil (t1 * (float) steps));
                        if (i1 - i0 < 1) continue;
                        juce::Path dash;
                        dash.startNewSubPath (pts[(size_t) i0]);
                        for (int i = i0 + 1; i <= i1; ++i) dash.lineTo (pts[(size_t) i]);
                        strokeBoth (dash, lineAlpha, 3.0f + 3.0f * v);
                    }
                }
            }
    }

private:
    void timerCallback() override
    {
        constexpr float dt = 1.0f / 30.0f;
        const auto target = (float) slider.valueToProportionOfLength (slider.getValue());
        level += (target - level) * 0.15f;
        time += dt * (0.4f + 1.6f * level);
        repaint (juce::Rectangle<float> (centre, centre).expanded (ringR * 1.35f + 8.0f).toNearestInt());
    }

    juce::Slider& slider;
    juce::Point<float> centre;
    float ringR = 0.0f, level = 0.0f, time = 0.0f;
};

// The heart of the plugin: one big Flux knob that feeds the field with energy and movement, from a
// still filter (0) to a chaotic, living one (100). It drives drift speed, lifespan, coupling and the
// life cycle on its own; Wander (how far the drift roams) and Rasp (roughness) sit beside it.
// The big knob sits at the horizontal centre of the window; Wander and Rasp are stacked to its right.
class FluxSection : public juce::Component
{
public:
    explicit FluxSection (ModContext& ctx)
        : amount (ctx, spec::get (spec::FP::FluxAmount).id, "Flux", KnobSize::Hero),
          wander (ctx, spec::get (spec::FP::Wander).id, "Wander"),
          rasp (ctx, spec::get (spec::FP::Rasp).id, "Rasp")
    {
        amount.getSlider().setTooltip ("Flux: feeds the field with energy and movement. 0 = a still formant filter; "
                                       "up: faster drift, shorter lives, cells exciting each other, more chaos");
        wander.getSlider().setTooltip ("Wander: how far the throat drifts (times Flux)");
        rasp.getSlider().setTooltip ("Rasp: roughness, jitter and a sub-octave growl (times Flux)");
        for (auto* c : { &amount, &wander, &rasp })
            addAndMakeVisible (c);
    }

    void paint (juce::Graphics& g) override { paintPanel (g, getLocalBounds(), "Flux", colours::flux); }

    // For the magnetic field overlay: the big knob and its value ring (in this section's coordinates).
    juce::Slider& getFluxSlider() noexcept { return amount.getSlider(); }
    juce::Rectangle<float> getRing() const noexcept { return ring; }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 0);
        r.removeFromTop (28);
        r.removeFromBottom (8);
        // The big knob at the horizontal centre of the whole window.
        const int parentW = getParentWidth() > 0 ? getParentWidth() : getWidth();
        const int hero = juce::jmin (amount.preferredHeight(), r.getHeight());
        const int heroW = amount.preferredWidth();
        const auto heroArea = juce::Rectangle<int> (parentW / 2 - getX() - heroW / 2, r.getCentreY() - hero / 2, heroW, hero);
        amount.setBounds (heroArea);
        auto& slider = amount.getSlider();
        ring = FluxLookAndFeel::knobBounds (getLookAndFeel().getSliderLayout (slider).sliderBounds)
                   .translated ((float) (slider.getX() + heroArea.getX()), (float) (slider.getY() + heroArea.getY()));

        // Wander and Rasp stacked in the space to its right.
        auto right = r.withLeft (heroArea.getRight());
        const int kw = wander.preferredWidth(), kh = wander.preferredHeight();
        const int gapY = juce::jlimit (0, 16, right.getHeight() - 2 * kh);
        const auto column = right.withSizeKeepingCentre (kw, juce::jmin (right.getHeight(), 2 * kh + gapY));
        wander.setBounds (column.withHeight (kh));
        rasp.setBounds (column.withTrimmedTop (kh + gapY));
    }

private:
    ParamControl amount, wander, rasp;
    juce::Rectangle<float> ring;
};

// Peak meters for L/R with clip LEDs. A LED lights when the signal went over 0 dBFS before the
// safety limiter (so it shows the limiter at work) and stays lit for a moment; click to clear.
class LevelMeter : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit LevelMeter (FluxFilterAudioProcessor& p) : processor (p)
    {
        setTooltip ("Output level. The LED lights when the signal hits 0 dBFS before the limiter.");
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto scale = r.removeFromRight (20.0f);
        const float barW = (r.getWidth() - 4.0f) / 2.0f;
        auto ledArea = r.removeFromTop (10.0f);
        r.removeFromTop (4.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = r.getX() + (float) ch * (barW + 4.0f);
            const auto led = juce::Rectangle<float> (x, ledArea.getY(), barW, ledArea.getHeight());
            g.setColour (clipHold[(size_t) ch] > 0 ? colours::clip : colours::track);
            g.fillRoundedRectangle (led, 2.0f);

            const auto bar = juce::Rectangle<float> (x, r.getY(), barW, r.getHeight());
            g.setColour (colours::background);
            g.fillRoundedRectangle (bar, 2.0f);
            const float h = bar.getHeight() * toY (level[(size_t) ch]);
            const auto fill = bar.withTop (bar.getBottom() - h);
            g.setGradientFill (juce::ColourGradient (colours::accent, 0.0f, bar.getBottom(), colours::flux, 0.0f, bar.getY(), false));
            g.fillRect (fill);
            const float py = bar.getBottom() - bar.getHeight() * toY (peakHold[(size_t) ch]);
            g.setColour (colours::text.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (bar.getX(), py - 1.0f, barW, 1.5f));
        }

        g.setFont (juce::FontOptions (9.0f));
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
        {
            const float y = juce::jlimit (r.getY() + 5.0f, r.getBottom() - 5.0f,
                                          r.getBottom() - r.getHeight() * toY (juce::Decibels::decibelsToGain (db)));
            g.setColour (colours::dim);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (scale.getX() + 2.0f, y - 5.0f, 18.0f, 10.0f), juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent&) override { clipHold = { 0, 0 }; repaint(); }

private:
    static float toY (float gain)   // -48..+0 dB → 0..1
    {
        const float db = juce::Decibels::gainToDecibels (gain, -100.0f);
        return juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
    }

    void timerCallback() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto c = (size_t) ch;
            const float peak = processor.takeOutputPeak (ch);
            level[c] = juce::jmax (peak, level[c] * 0.86f);               // ~20 dB/s fall
            if (peak >= peakHold[c]) { peakHold[c] = peak; peakAge[c] = 0; }
            else if (++peakAge[c] > 45) peakHold[c] *= 0.9f;
            if (processor.takeClip (ch)) clipHold[c] = 45;               // 1.5 s
            else if (clipHold[c] > 0) --clipHold[c];
        }
        repaint();
    }

    FluxFilterAudioProcessor& processor;
    std::array<float, 2> level {}, peakHold {};
    std::array<int, 2> peakAge {}, clipHold {};
};

// Compact output strip next to the field display: meters, Mix and Output, Auto Gain and Limiter.
class OutputSection : public juce::Component
{
public:
    explicit OutputSection (ModContext& ctx)
        : meter (ctx.processor),
          mix (ctx, spec::get (spec::FP::Mix).id, "Mix"),
          output (ctx, spec::get (spec::FP::Output).id, "Gain")
    {
        auto pill = [&] (juce::TextButton& b, const char* text, const char* paramId, const char* tip,
                         std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& att) {
            b.setButtonText (text);
            b.setClickingTogglesState (true);
            b.setTooltip (tip);
            addAndMakeVisible (b);
            att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ctx.state, paramId, b);
        };
        pill (autoGain, "AUTO", params::id::autoGain, "Auto Gain: turns the filtered signal down when resonance, drive or feedback make it louder than the input (never up)", autoGainAtt);
        pill (limiter, "LIMIT", params::id::limiter, "Soft safety limiter at 0 dBFS", limiterAtt);
        for (auto* c : std::initializer_list<juce::Component*> { &meter, &mix, &output })
            addAndMakeVisible (c);
    }

    void paint (juce::Graphics& g) override { paintPanel (g, getLocalBounds(), "Output"); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 0);
        r.removeFromTop (28);
        r.removeFromBottom (8);
        meter.setBounds (r.removeFromLeft (44));   // meters on the left, knobs and switches on the right
        r.removeFromLeft (8);
        auto pills = r.removeFromBottom (20);
        autoGain.setBounds (pills.removeFromLeft (pills.getWidth() / 2).withTrimmedRight (2));
        limiter.setBounds (pills.withTrimmedLeft (2));
        r.removeFromBottom (4);
        auto top = r.removeFromTop (r.getHeight() / 2);
        mix.setBounds (top.withSizeKeepingCentre (mix.preferredWidth(), juce::jmin (top.getHeight(), mix.preferredHeight())));
        output.setBounds (r.withSizeKeepingCentre (output.preferredWidth(), juce::jmin (r.getHeight(), output.preferredHeight())));
    }

private:
    LevelMeter meter;
    ParamControl mix, output;
    juce::TextButton autoGain, limiter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAtt, limiterAtt;
};

// Small cross button (remove a connection).
class CrossButton : public juce::Button
{
public:
    CrossButton() : juce::Button ("remove") { setTooltip ("Remove this modulation"); }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (8.0f, 8.0f);
        g.setColour (over ? colours::text : colours::dim);
        g.drawLine ({ r.getTopLeft(), r.getBottomRight() }, 1.5f);
        g.drawLine ({ r.getBottomLeft(), r.getTopRight() }, 1.5f);
    }
};

// Shows the selected knob's modulation: one row per connected source with a depth bar.
class ModPanel : public juce::Component, private juce::Timer
{
public:
    explicit ModPanel (ModContext& context) : ctx (context) { startTimerHz (15); }

    void paint (juce::Graphics& g) override
    {
        paintPanel (g, getLocalBounds(), "Modulation");
        auto body = getLocalBounds().reduced (12, 0).withTrimmedTop (28).withTrimmedBottom (10);

        if (paramName.isNotEmpty())
        {
            g.setColour (colours::accent);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (paramName, getLocalBounds().removeFromTop (28).withTrimmedRight (12), juce::Justification::centredRight);
        }

        juce::String hint;
        if (ctx.getSelected().isEmpty())
            hint = "Drag an LFO's move icon onto a knob to modulate it. Click a knob to see and edit its modulation here.";
        else if (target == ModTarget::None)
            hint = paramName + " can't be modulated.";
        else if (rows.empty())
            hint = "Nothing modulates " + paramName + " yet. Drag an LFO's move icon onto it.";
        if (hint.isNotEmpty())
        {
            g.setColour (colours::dim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText (hint, body.withTrimmedTop (6), juce::Justification::topLeft, 4);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 0).withTrimmedTop (30);
        for (auto& row : rows)
        {
            row->setBounds (r.removeFromTop (38));
            r.removeFromTop (4);
        }
    }

private:
    class Row : public juce::Component
    {
    public:
        Row (ModContext& c, const ModContext::Connection& conn) : ctx (c), slot (conn.slot), source (conn.source)
        {
            depth.setSliderStyle (juce::Slider::LinearHorizontal);
            depth.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            depth.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            depth.setColour (juce::Slider::trackColourId, colours::source (source));
            depth.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            depth.onValueChange = [this] { repaint(); };
            depth.setDoubleClickReturnValue (true, 0.0);
            addAndMakeVisible (depth);
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ctx.state, ctx.depthParamId (slot), depth);
            remove.onClick = [this] { auto& cx = ctx; const int s = slot; juce::MessageManager::callAsync ([&cx, s] { cx.disconnect (s); }); };
            addAndMakeVisible (remove);
        }

        // Top line: source and depth value; under it the depth bar across the whole row.
        void paint (juce::Graphics& g) override
        {
            auto line = getLocalBounds().removeFromTop (labelH).withTrimmedRight (26);
            g.setColour (colours::source (source));
            g.fillEllipse (line.removeFromLeft (14).withSizeKeepingCentre (9, 9).toFloat());
            g.setColour (colours::text);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (modSourceNames[(int) source], line.withTrimmedLeft (4), juce::Justification::centredLeft);
            g.drawText (depth.getTextFromValue (depth.getValue()), line, juce::Justification::centredRight);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            remove.setBounds (r.removeFromRight (22).removeFromTop (labelH));
            r.removeFromTop (labelH);
            depth.setBounds (r.withTrimmedRight (4).withTrimmedLeft (2));
        }

    private:
        static constexpr int labelH = 16;
        ModContext& ctx;
        int slot;
        ModSource source;
        juce::Slider depth;
        CrossButton remove;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override
    {
        const auto& sel = ctx.getSelected();
        const auto t = ModContext::targetFor (sel);
        juce::String sig = sel;
        const auto list = ctx.connectionsFor (t);
        for (const auto& c : list) sig << "|" << c.slot << ":" << (int) c.source;

        if (sig != signature)
        {
            signature = sig;
            target = t;
            auto* p = sel.isNotEmpty() ? ctx.state.getParameter (sel) : nullptr;
            paramName = p != nullptr ? p->getName (40) : juce::String();
            rows.clear();
            for (const auto& c : list)
                addAndMakeVisible (*rows.emplace_back (std::make_unique<Row> (ctx, c)));
            resized();
            repaint();
        }
    }

    ModContext& ctx;
    juce::String signature, paramName;
    ModTarget target = ModTarget::None;
    std::vector<std::unique_ptr<Row>> rows;
};

} // namespace flux::ui
