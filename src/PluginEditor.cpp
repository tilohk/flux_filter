#include "PluginEditor.h"

using namespace flux;
using namespace flux::ui;
using spec::FP;

namespace
{
    constexpr int headerH = 40, fieldViewH = 190, margin = 12, gap = 10;
    const char* id (FP p) { return spec::get (p).id; }
}

// ---------------------------------------------------------------------------------------------
ParamSection& FluxFilterAudioProcessorEditor::SectionPage::addSection (const juce::String& title, int column, float widthWeight)
{
    auto& e = entries.emplace_back (Entry { std::make_unique<ParamSection> (title), column, widthWeight });
    addAndMakeVisible (*e.section);
    return *e.section;
}

void FluxFilterAudioProcessorEditor::SectionPage::resized()
{
    int numColumns = 0;
    for (auto& e : entries) numColumns = juce::jmax (numColumns, e.column + 1);
    if (numColumns == 0) return;

    auto r = getLocalBounds().reduced (margin);
    const int colW = (r.getWidth() - gap * (numColumns - 1)) / numColumns;
    for (int c = 0; c < numColumns; ++c)
    {
        auto col = r.withX (r.getX() + c * (colW + gap)).withWidth (colW);
        for (auto& e : entries)
            if (e.column == c)
            {
                const int h = e.section->heightFor (colW);
                e.section->setBounds (col.removeFromTop (h));
                col.removeFromTop (gap);
            }
    }
}

// ---------------------------------------------------------------------------------------------
FluxFilterAudioProcessorEditor::ModPage::ModPage (FluxFilterAudioProcessor& p)
{
    auto& s = p.apvts;
    const FP rates[] = { FP::Lfo1Rate, FP::Lfo2Rate, FP::Lfo3Rate };
    const FP phases[] = { FP::Lfo1Phase, FP::Lfo2Phase, FP::Lfo3Phase };

    for (int i = 0; i < ModSettings::numLfos; ++i)
    {
        auto& sec = *lfoSections.emplace_back (std::make_unique<ParamSection> ("LFO " + juce::String (i + 1)));
        sec.add (s, id (rates[i]), "Rate");
        sec.add (s, id (phases[i]), "Phase");
        sec.add (s, params::id::lfo (i, "Sync"), "Sync");
        sec.add (s, params::id::lfo (i, "Division"), "Division");
        sec.add (s, params::id::lfo (i, "Shape"), "Shape");
        sec.add (s, params::id::lfo (i, "Polarity"), "Polarity");

        auto& stack = *stacks.emplace_back (std::make_unique<Stack>());
        stack.add (std::make_unique<CurveEditor> (p, i), 88);
        sec.setExtra (&stack, stack.total());
        addAndMakeVisible (sec);
    }

    auto lower = [&] (const juce::String& title) -> ParamSection& {
        auto& sec = *lowerSections.emplace_back (std::make_unique<ParamSection> (title));
        addAndMakeVisible (sec);
        return sec;
    };
    auto meters = [&] (ParamSection& sec, std::initializer_list<std::tuple<ModSource, const char*, bool>> list) {
        auto& stack = *stacks.emplace_back (std::make_unique<Stack>());
        for (auto& [src, name, bipolar] : list)
            stack.add (std::make_unique<SourceMeter> (p, src, name, bipolar), 14);
        sec.setExtra (&stack, stack.total());
    };

    auto& env = lower ("Envelope");
    env.add (s, id (FP::EnvAttack), "Attack");
    env.add (s, id (FP::EnvRelease), "Release");
    env.add (s, id (FP::EnvSens), "Sensitivity");
    meters (env, { { ModSource::Env, "Envelope", false }, { ModSource::Transient, "Transient", false }, { ModSource::Body, "Body", false } });

    auto& chaos = lower ("Chaos");
    chaos.add (s, id (FP::ChaosSpeed), "Speed");
    chaos.add (s, id (FP::ChaosShape), "Shape");
    meters (chaos, { { ModSource::ChaosX, "X", true }, { ModSource::ChaosY, "Y", true } });

    auto& drift = lower ("Drift");
    drift.add (s, id (FP::DriftRate), "Rate");
    drift.add (s, params::id::driftMode, "Mode");
    drift.add (s, id (FP::DriftSlew), "Slew");
    meters (drift, { { ModSource::Drift, "Drift", true } });

    auto& macros = lower ("Macros");
    macros.add (s, id (FP::Macro1), "1");
    macros.add (s, id (FP::Macro2), "2");
    macros.add (s, id (FP::Macro3), "3");
    macros.add (s, id (FP::Macro4), "4");
}

void FluxFilterAudioProcessorEditor::ModPage::resized()
{
    auto r = getLocalBounds().reduced (margin);

    const int lfoW = (r.getWidth() - gap * 2) / 3;
    int lfoH = 0;
    for (auto& s : lfoSections) lfoH = juce::jmax (lfoH, s->heightFor (lfoW));
    auto top = r.removeFromTop (lfoH);
    for (auto& s : lfoSections) { s->setBounds (top.removeFromLeft (lfoW)); top.removeFromLeft (gap); }
    r.removeFromTop (gap);

    // Lower row: every section gets the width its controls need in one row, plus a share of what's left.
    const int avail = r.getWidth() - gap * ((int) lowerSections.size() - 1);
    int needed = 0;
    for (auto& s : lowerSections) needed += s->singleRowWidth();
    const int spare = juce::jmax (0, avail - needed) / (int) lowerSections.size();
    int lowerH = 0;
    std::vector<int> widths;
    for (auto& s : lowerSections)
    {
        widths.push_back (needed <= avail ? s->singleRowWidth() + spare : s->singleRowWidth() * avail / needed);
        lowerH = juce::jmax (lowerH, s->heightFor (widths.back()));
    }
    auto bottom = r.removeFromTop (lowerH);
    for (size_t i = 0; i < lowerSections.size(); ++i)
    {
        lowerSections[i]->setBounds (bottom.removeFromLeft (widths[i]));
        bottom.removeFromLeft (gap);
    }
}

// ---------------------------------------------------------------------------------------------
FluxFilterAudioProcessorEditor::FluxFilterAudioProcessorEditor (FluxFilterAudioProcessor& p)
    : AudioProcessorEditor (&p), fieldView (p), modPage (p), matrixView (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& s = p.apvts;

    auto& field = fieldPage.addSection ("Field", 0);
    field.add (s, params::id::density, "Density");
    field.add (s, id (FP::Center), "Center");
    field.add (s, id (FP::Spread), "Spread");
    field.add (s, params::id::scale, "Scale");
    field.add (s, id (FP::Tilt), "Tilt");
    field.add (s, id (FP::Resonance), "Resonance");
    field.add (s, id (FP::Morph), "Morph");
    field.add (s, id (FP::Combine), "Par / Ser");
    field.add (s, id (FP::Width), "Width");

    auto& character = fieldPage.addSection ("Character", 0);
    character.add (s, params::id::character, "Character");
    character.add (s, id (FP::Drift), "Drift");
    character.add (s, id (FP::Drive), "Drive");
    character.add (s, id (FP::Feedback), "Feedback");
    character.add (s, id (FP::Damping), "Damping");

    auto& flux = fieldPage.addSection ("Flux", 1);
    flux.add (s, id (FP::FluxRate), "Rate");
    flux.add (s, params::id::fluxShape, "Shape");
    flux.add (s, id (FP::FluxDepth), "Depth");
    flux.add (s, id (FP::FluxLevel), "Level");
    flux.add (s, id (FP::Travel), "Travel");
    flux.add (s, params::id::direction, "Direction");
    flux.add (s, id (FP::Viscosity), "Viscosity");
    flux.add (s, id (FP::Wavelength), "Wavelength");
    flux.add (s, id (FP::Turbulence), "Turbulence");
    flux.add (s, id (FP::FluxStereo), "Stereo");

    auto& output = fieldPage.addSection ("Output", 1);
    output.add (s, params::id::autoGain, "Auto Gain");
    output.add (s, params::id::limiter, "Limiter");
    output.add (s, id (FP::Mix), "Mix");
    output.add (s, id (FP::Output), "Output");

    tabs.setTabBarDepth (30);
    tabs.addTab ("Field", colours::background, &fieldPage, false);
    tabs.addTab ("Modulation", colours::background, &modPage, false);
    tabs.addTab ("Matrix", colours::background, &matrixView, false);

    addAndMakeVisible (fieldView);
    addAndMakeVisible (tabs);

    setResizable (true, true);
    setResizeLimits (1100, 860, 1800, 1300);
    setSize (1180, 880);

    // Pages built before they joined this editor still carry the default look; refresh everything.
    sendLookAndFeelChange();

   #if JUCE_DEBUG
    // Dev aid: FLUXFILTER_UI_SNAPSHOT=/some/dir renders every tab to PNG (debug builds only).
    const auto snapshotDir = juce::SystemStats::getEnvironmentVariable ("FLUXFILTER_UI_SNAPSHOT", {});
    if (snapshotDir.isNotEmpty())
        juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer<FluxFilterAudioProcessorEditor> (this), snapshotDir] {
            if (safe != nullptr) safe->writeSnapshots (juce::File (snapshotDir));
        });
   #endif
}

#if JUCE_DEBUG
void FluxFilterAudioProcessorEditor::writeSnapshots (const juce::File& dir)
{
    dir.createDirectory();
    for (int i = 0; i < tabs.getNumTabs(); ++i)
    {
        tabs.setCurrentTabIndex (i);
        const auto image = createComponentSnapshot (getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out (dir.getChildFile ("ui_" + tabs.getTabNames()[i].toLowerCase() + ".png"));
        if (out.openedOk())
        {
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
    tabs.setCurrentTabIndex (0);
}
#endif

FluxFilterAudioProcessorEditor::~FluxFilterAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void FluxFilterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    auto header = getLocalBounds().removeFromTop (headerH).reduced (margin, 0);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText (JucePlugin_Name, header, juce::Justification::centredLeft);

    // Legend for the field display: left channel / right channel.
    g.setFont (juce::FontOptions (12.0f));
    auto legend = header.removeFromRight (150);
    auto item = [&] (juce::Rectangle<int> area, juce::Colour c, const juce::String& text) {
        g.setColour (c);
        g.fillEllipse (area.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat());
        g.setColour (colours::dim);
        g.drawText (text, area.withTrimmedLeft (5), juce::Justification::centredLeft);
    };
    item (legend.removeFromLeft (75), colours::accent, "Left");
    item (legend, colours::accent2, "Right");

    g.setColour (colours::dim);
    g.drawText ("v" JucePlugin_VersionString, header.withTrimmedRight (12), juce::Justification::centredRight);
}

void FluxFilterAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (headerH);
    fieldView.setBounds (r.removeFromTop (fieldViewH).reduced (margin, 0));
    r.removeFromTop (6);
    tabs.setBounds (r);
}
