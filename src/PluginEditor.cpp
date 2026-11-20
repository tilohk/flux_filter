#include "PluginEditor.h"

using namespace flux;
using namespace flux::ui;
using spec::FP;

namespace
{
    // Header matches audible_spec: 14 px margin, 30 px high, title right-aligned.
    constexpr int headerTop = 14, headerH = 30, topRowH = 256, bottomH = 190, margin = 12, gap = 10,
                  characterW = 174,          // 10 px from the outer knobs to the border
                  outputW = characterW;
    const char* id (FP p) { return spec::get (p).id; }
}

// ---------------------------------------------------------------------------------------------
FluxFilterAudioProcessorEditor::Panel::Panel (FluxFilterAudioProcessor& p)
    : ctx (p), fieldView (p), flux (ctx), output (ctx), modPanel (ctx)
{
    cells.add (ctx, params::id::density, "Density");
    cells.add (ctx, id (FP::Center), "Center");
    cells.add (ctx, id (FP::Spread), "Spread");
    cells.add (ctx, id (FP::Resonance), "Resonance");
    cells.add (ctx, id (FP::Morph), "Morph");
    cells.add (ctx, id (FP::Width), "Width");

    character.add (ctx, params::id::character, "Character", true).showStepMarkers();
    character.add (ctx, id (FP::Drive), "Drive");
    character.add (ctx, id (FP::Feedback), "Feedback");
    character.add (ctx, id (FP::Damping), "Damping");

    for (int i = 0; i < ModSettings::numLfos; ++i)
        addAndMakeVisible (*lfos.emplace_back (std::make_unique<LfoModule> (ctx, i)));

    for (auto* c : std::initializer_list<juce::Component*> { &fieldView, &cells, &character, &flux, &output, &modPanel })
        addAndMakeVisible (c);
}

void FluxFilterAudioProcessorEditor::Panel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Same caption as audible_spec (Avenir Next Demi Bold 25, right-aligned), in the flux colour.
    const auto header = getLocalBounds().withTrimmedTop (headerTop).removeFromTop (headerH).reduced (margin, 0);
    g.setColour (colours::flux);
    g.setFont (juce::FontOptions().withName ("Avenir Next").withStyle ("Demi Bold").withHeight (25.0f));
    g.drawText (JucePlugin_Name, header, juce::Justification::centredRight);

    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("v" JucePlugin_VersionString, header, juce::Justification::centredLeft);
}

void FluxFilterAudioProcessorEditor::Panel::resized()
{
    auto r = getLocalBounds().reduced (margin, 0);
    r.removeFromTop (headerTop + headerH + gap);
    r.removeFromBottom (margin);

    // Top: field display, output strip beside it.
    auto top = r.removeFromTop (topRowH);
    output.setBounds (top.removeFromRight (outputW));
    top.removeFromRight (gap);
    fieldView.setBounds (top);
    r.removeFromTop (gap);

    // Bottom: LFO 1-3 and the selected knob's modulation, all the same width.
    auto bottom = r.removeFromBottom (bottomH);
    r.removeFromBottom (gap);
    const int moduleW = (bottom.getWidth() - 3 * gap) / 4;
    for (auto& l : lfos) { l->setBounds (bottom.removeFromLeft (moduleW)); bottom.removeFromLeft (gap); }
    modPanel.setBounds (bottom);

    // Middle: Cells | Flux (takes the rest) | Character.
    cells.setBounds (r.removeFromLeft ((r.getWidth() - 3 * gap) / 4));   // as wide as one LFO module
    r.removeFromLeft (gap);
    character.setBounds (r.removeFromRight (characterW));
    r.removeFromRight (gap);
    flux.setBounds (r);
}

// ---------------------------------------------------------------------------------------------
FluxFilterAudioProcessorEditor::FluxFilterAudioProcessorEditor (FluxFilterAudioProcessor& p)
    : AudioProcessorEditor (&p), panel (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (panel);
    panel.setBounds (0, 0, designWidth, designHeight);

    setResizable (true, true);
    setResizeLimits (designWidth * 3 / 4, designHeight * 3 / 4, designWidth * 3 / 2, designHeight * 3 / 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    setSize (designWidth, designHeight);

    // Children built before they joined this editor still carry the default look; refresh everything.
    sendLookAndFeelChange();

   #if JUCE_DEBUG
    // Dev aid: FLUXFILTER_UI_SNAPSHOT=/some/dir renders the panel to PNG (debug builds only).
    // FLUXFILTER_UI_DEMO=1 also adds a few connections first, to show the modulation display.
    const auto snapshotDir = juce::SystemStats::getEnvironmentVariable ("FLUXFILTER_UI_SNAPSHOT", {});
    if (snapshotDir.isNotEmpty())
    {
        if (juce::SystemStats::getEnvironmentVariable ("FLUXFILTER_UI_DEMO", {}).isNotEmpty())
        {
            panel.ctx.connect (ModSource::Lfo1, ModTarget::Cutoff, 0.2f);
            panel.ctx.connect (ModSource::Lfo3, ModTarget::Cutoff, -0.1f);
            panel.ctx.connect (ModSource::Lfo2, ModTarget::Resonance, 0.2f);
            panel.ctx.connect (ModSource::Lfo3, ModTarget::FluxAmount, 0.3f);
            panel.ctx.connect (ModSource::Lfo2, ModTarget::Drive, 0.3f);
            panel.ctx.select (spec::get (FP::Center).id);

            // Test signal for the analyser: pinkish noise plus a few partials.
            auto feed = std::make_shared<std::function<void()>>();
            *feed = [&proc = p, feed, n = 0] () mutable {
                static juce::Random rng (7);
                static float b0 = 0, b1 = 0, b2 = 0;
                std::vector<float> buf (1600);
                for (auto& x : buf)
                {
                    const float w = rng.nextFloat() * 2.0f - 1.0f;
                    b0 = 0.997f * b0 + 0.029f * w; b1 = 0.985f * b1 + 0.032f * w; b2 = 0.95f * b2 + 0.048f * w;
                    const float t = (float) n++ / 48000.0f;
                    x = 0.4f * (b0 + b1 + b2) + 0.2f * std::sin (6.2831853f * 110.0f * t)
                      + 0.08f * std::sin (6.2831853f * 330.0f * t) + 0.04f * std::sin (6.2831853f * 1320.0f * t);
                }
                proc.debugPushAnalyserSamples (buf.data(), (int) buf.size());
                if (n < 48000 * 3) juce::Timer::callAfterDelay (33, *feed);
            };
            (*feed)();
        }
        juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer<FluxFilterAudioProcessorEditor> (this), snapshotDir] {
            if (safe != nullptr) safe->writeSnapshot (juce::File (snapshotDir));
        });
    }
   #endif
}

#if JUCE_DEBUG
void FluxFilterAudioProcessorEditor::writeSnapshot (const juce::File& dir)
{
    dir.createDirectory();
    const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
    juce::FileOutputStream out (dir.getChildFile ("ui_panel.png"));
    if (out.openedOk())
    {
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    }
}
#endif

FluxFilterAudioProcessorEditor::~FluxFilterAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void FluxFilterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void FluxFilterAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) designWidth;
    panel.setTransform (juce::AffineTransform::scale (scale));
}
