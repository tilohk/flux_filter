#pragma once

#include "ParamControl.h"

namespace flux::ui
{

// The heart of the plugin: one big Flux knob (how strongly the wave moves the field) surrounded
// by the controls that shape how the wave travels. The big knob sits at the horizontal centre of
// the window, whatever the section's own position and width.
class FluxSection : public juce::Component
{
public:
    explicit FluxSection (ModContext& ctx)
        : amount (ctx, spec::get (spec::FP::FluxAmount).id, "Flux", KnobSize::Hero),
          shape (ctx, params::id::fluxShape, "Shape", [] (int i, juce::Rectangle<float> a) {
              switch (i)
              {
                  case 1:  return icons::triangle (a);
                  case 2:  return icons::rampUp (a);
                  case 3:  return icons::random (a);
                  default: return icons::sine (a);
              }
          }, colours::flux),
          direction (ctx, params::id::direction, "Direction", [] (int i, juce::Rectangle<float> a) {
              juce::Path p;   // the field runs low -> high from left to right
              switch (i)
              {
                  case 1:  icons::arrow (p, a, 1.0f, -1.0f); break;                                   // Down
                  case 2:  icons::arrow (p, a, -0.15f, -1.0f); icons::arrow (p, a, 0.15f, 1.0f); break;  // Outward
                  case 3:  icons::arrow (p, a, -1.0f, -0.15f); icons::arrow (p, a, 1.0f, 0.15f); break;  // Inward
                  default: icons::arrow (p, a, -1.0f, 1.0f); break;                                   // Up
              }
              return p;
          }, colours::flux)
    {
        auto knob = [&] (spec::FP p, const char* label) -> ParamControl& {
            auto& c = *knobs.emplace_back (std::make_unique<ParamControl> (ctx, spec::get (p).id, label));
            addAndMakeVisible (c);
            return c;
        };
        // Left: Lifespan, Stereo / Coupling, Feed. Right: Drift, Wander with Shape and Direction under them.
        knob (spec::FP::Lifespan, "Lifespan");
        knob (spec::FP::FluxStereo, "Stereo");
        knob (spec::FP::Coupling, "Coupling");
        knob (spec::FP::Feed, "Feed");
        knob (spec::FP::Drift, "Drift");
        knob (spec::FP::Wander, "Wander");
        amount.getSlider().setTooltip ("Flux: how strongly the wave moves through the field");
        addAndMakeVisible (amount);
        addAndMakeVisible (shape);
        addAndMakeVisible (direction);
    }

    void paint (juce::Graphics& g) override
    {
        paintPanel (g, getLocalBounds(), "Flux", colours::flux);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 0);
        r.removeFromTop (28);
        r.removeFromBottom (8);
        const int parentW = getParentWidth() > 0 ? getParentWidth() : getWidth();

        // Buttons as wide as the LFO shape buttons (four equal modules in the bottom row, five buttons each).
        const int moduleW = (parentW - 2 * 12 - 3 * 10) / 4;
        const int bw = (moduleW - 20 - 4 * ChoiceButtonRow::gapW) / 5;
        shape.setButtonWidth (bw);
        direction.setButtonWidth (bw);

        // The big knob at the window's horizontal centre.
        const int hero = juce::jmin (210, r.getHeight());
        const int centreX = parentW / 2 - getX();
        const auto heroArea = juce::Rectangle<int> (centreX - hero / 2, r.getCentreY() - hero / 2, hero, hero);
        amount.setBounds (heroArea);

        auto place = [] (ParamControl& k, juce::Rectangle<int> cell) {
            k.setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), k.preferredWidth()), k.preferredHeight()));
        };

        // Left: 2 x 2 grid.
        const auto leftArea = r.withRight (heroArea.getX() - 8);
        const int knobH = knobs[0]->preferredHeight();
        const int leftW = juce::jmin (leftArea.getWidth(), 160);
        const auto left = leftArea.withLeft (leftArea.getRight() - leftW).withSizeKeepingCentre (leftW, 2 * knobH);
        for (int i = 0; i < 4; ++i)
            place (*knobs[(size_t) i], { left.getX() + (i % 2) * leftW / 2, left.getY() + (i / 2) * knobH, leftW / 2, knobH });

        // Right: Rate, Travel, then the Shape and Direction buttons.
        auto right = r.withLeft (heroArea.getRight() + 8);
        right = right.withSizeKeepingCentre (right.getWidth(), knobH + 10 + shape.preferredHeight() + 8 + direction.preferredHeight());
        auto row = right.removeFromTop (knobH).withSizeKeepingCentre (shape.rowWidth(), knobH);
        for (int i = 4; i < 6; ++i) place (*knobs[(size_t) i], row.removeFromLeft (shape.rowWidth() / 2));
        right.removeFromTop (10);
        shape.setBounds (right.removeFromTop (shape.preferredHeight()));
        right.removeFromTop (8);
        direction.setBounds (right);
    }

private:
    ParamControl amount;
    ChoiceButtonRow shape, direction;
    std::vector<std::unique_ptr<ParamControl>> knobs;
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
            row->setBounds (r.removeFromTop (28));
    }

private:
    class Row : public juce::Component
    {
    public:
        Row (ModContext& c, const ModContext::Connection& conn, ModTarget connTarget) : ctx (c), slot (conn.slot), source (conn.source)
        {
            depth.setSliderStyle (juce::Slider::LinearHorizontal);
            depth.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            depth.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            depth.setColour (juce::Slider::trackColourId, colours::source (source));
            depth.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
            depth.setDoubleClickReturnValue (true, 0.0);
            addAndMakeVisible (depth);
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ctx.state, ctx.depthParamId (slot), depth);
            remove.onClick = [this] { auto& cx = ctx; const int s = slot; juce::MessageManager::callAsync ([&cx, s] { cx.disconnect (s); }); };
            addAndMakeVisible (remove);

            const int lfo = (int) source - (int) ModSource::Lfo1;
            if (isLfo (source) && isCellTarget (connTarget))
                fluxParam = ctx.state.getRawParameterValue (params::id::lfo (lfo, "Flux"));
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();
            auto name = r.removeFromLeft (labelW);
            g.setColour (colours::source (source));
            g.fillEllipse (name.removeFromLeft (14).withSizeKeepingCentre (9, 9).toFloat());
            g.setColour (colours::text);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (modSourceNames[(int) source], name.withTrimmedLeft (4), juce::Justification::centredLeft);

            if (fluxParam != nullptr && fluxParam->load() > 0.001f)   // this LFO's movement travels through the field
            {
                const auto tag = name.removeFromRight (32).withSizeKeepingCentre (32, 14).toFloat();
                g.setColour (colours::flux.withAlpha (0.2f));
                g.fillRoundedRectangle (tag, 3.0f);
                g.setColour (colours::flux);
                g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                g.drawText ("FLUX", tag, juce::Justification::centred);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            r.removeFromLeft (labelW);
            remove.setBounds (r.removeFromRight (22));
            depth.setBounds (r.reduced (2, 2));
        }

    private:
        static constexpr int labelW = 98;
        ModContext& ctx;
        int slot;
        ModSource source;
        juce::Slider depth;
        CrossButton remove;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        std::atomic<float>* fluxParam = nullptr;
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
                addAndMakeVisible (*rows.emplace_back (std::make_unique<Row> (ctx, c, t)));
            resized();
            repaint();
        }
        else
        {
            for (auto& r : rows) r->repaint();   // flux tags follow the LFO switches
        }
    }

    ModContext& ctx;
    juce::String signature, paramName;
    ModTarget target = ModTarget::None;
    std::vector<std::unique_ptr<Row>> rows;
};

} // namespace flux::ui
