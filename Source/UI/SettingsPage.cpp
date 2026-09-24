//
//  SettingsPage.cpp
//  FX Analyzer
//

#include "SettingsPage.h"
#include "../PluginProcessor.h"

//==============================================================================
SettingsPage::SettingsPage (FXAnalyzerProcessor& processor)
    : PageBase (processor),
      preview (processor)
{
    preview.setEmbedded (true);
    addAndMakeVisible (preview);

    topRow =
    {
        { "RESOLUTION", { &resolutionStepper, &fftSizeStepper, &bandsStepper }, {} },
        { "RANGE",      { &topStepper, &rangeStepper },                          {} },
        { "DISPLAY",    { &viewStepper, &scaleStepper, &themeStepper },          {} }
    };

    bottomRow =
    {
        { "SMOOTHING",  { &reactivityStepper, &attackStepper, &releaseStepper }, {} },
        { "TILT",       { &slopeStepper, &pivotStepper },                        {} },
        { "INPUT",      { &channelStepper, &dcBlockStepper, &inputGainStepper }, {} }
    };

    // Compact, all of them. See the file header for why the large size had to go.
    for (auto* stepper : allSteppers())
    {
        stepper->setCompact (true);
        addAndMakeVisible (*stepper);
    }

    // ---- RESOLUTION ---------------------------------------------------------
    resolutionStepper.setLabel ("Mode");
    resolutionStepper.setItems (FXParams::spectrumResolutionNames);

    fftSizeStepper.setLabel ("FFT Size");
    fftSizeStepper.setItems (FXParams::fftSizeNames);

    bandsStepper.setLabel ("Bands");
    bandsStepper.setItems (FXParams::spectrumSmoothingNames);

    // ---- RANGE --------------------------------------------------------------
    topStepper.setLabel ("Top");
    topStepper.setItems (FXParams::spectrumTopNames);

    rangeStepper.setLabel ("Range");
    rangeStepper.setItems (FXParams::spectrumRangeNames);

    // ---- TILT ---------------------------------------------------------------
    slopeStepper.setLabel ("Slope");
    slopeStepper.setNumericRange (FXParams::spectrumTiltMinDb, FXParams::spectrumTiltMaxDb,
                                  FXParams::spectrumTiltStepDb, FXParams::defaultSpectrumTiltDb(),
                                  [] (float db) { return SpectrumPage::formatTilt (db); });

    pivotStepper.setLabel ("Pivot");
    pivotStepper.setItems (FXParams::spectrumTiltPivotNames);

    // ---- SMOOTHING ----------------------------------------------------------
    reactivityStepper.setLabel ("Reactivity");
    reactivityStepper.setItems (FXParams::reactivityNames);

    attackStepper.setLabel ("Attack");
    attackStepper.setItems (FXParams::spectrumAttackNames);

    releaseStepper.setLabel ("Release");
    releaseStepper.setItems (FXParams::spectrumReleaseNames);

    // ---- INPUT --------------------------------------------------------------
    channelStepper.setLabel ("Channel");
    channelStepper.setItems (FXParams::channelNames);

    dcBlockStepper.setLabel ("DC Block");
    dcBlockStepper.setItems ({ "Off", "On" });

    inputGainStepper.setLabel ("Input Gain");

    // Numeric rather than a list of choices - see StepperControl::setNumericRange.
    // One click is a decibel, shift a tenth, alt-click back to unity.
    inputGainStepper.setNumericRange (FXParams::inputGainMinDb, FXParams::inputGainMaxDb,
                                      1.0f, 0.0f,
                                      [] (float db)
    {
        const auto rounded = std::abs (db) < 0.05f ? 0.0f : db;

        if (std::abs (rounded - std::round (rounded)) < 0.05f)
            return rounded == 0.0f ? juce::String ("0 dB")
                                   : (rounded > 0.0f ? "+" : "") + juce::String ((int) std::round (rounded)) + " dB";

        return (rounded > 0.0f ? "+" : "") + juce::String (rounded, 1) + " dB";
    });

    // ---- DISPLAY ------------------------------------------------------------
    viewStepper.setLabel ("View");
    viewStepper.setItems (FXParams::spectrumModeNames);

    scaleStepper.setLabel ("Scale");
    scaleStepper.setItems (FXParams::frequencyScaleNames);

    themeStepper.setLabel ("Theme");
    {
        juce::StringArray names;

        for (const auto& theme : builtInThemes())
            names.add (theme.name);

        themeStepper.setItems (names);
    }

    //==============================================================================
    // Every callback guards on `syncing`, because syncFromState() sets the
    // controls and would otherwise write each value straight back to the host -
    // harmless while nothing else is moving, and an automation fight the moment
    // something is. Every one ends in updatePreview(), so the curve above
    // answers the step that was just made.

    resolutionStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        plugin.setViewProperty (FXParams::propSpectrumResolution,
                                juce::jlimit (0, FXParams::spectrumResolutionNames.size() - 1, index));
        plugin.applyDisplaySettings();
        updatePreview();
    };

    fftSizeStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        const auto order = FXParams::fftOrderValues[juce::jlimit (0, FXParams::lastFftSizeIndex, index)];

        plugin.setViewProperty (FXParams::propFftOrder, order);
        plugin.getAnalysis().setFftOrder (order);
        updatePreview();
    };

    bandsStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        plugin.setViewProperty (FXParams::propSpectrumSmoothing,
                                juce::jlimit (0, FXParams::spectrumSmoothingNames.size() - 1, index));

        // The whole set, not only the smoothing: Bands decides whether Multi
        // can be honoured at all - see FXParams::multiResolutionAvailable - so
        // stepping it to Off and back has to take the tiers with it.
        plugin.applyDisplaySettings();
        updatePreview();
    };

    topStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        plugin.setViewProperty (FXParams::propSpectrumTop, index);
        updatePreview();
    };

    rangeStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        plugin.setViewProperty (FXParams::propSpectrumRange, index);
        updatePreview();
    };

    slopeStepper.onNumericChange = [this] (float value)
    {
        if (syncing)
            return;

        plugin.setSpectrumTiltDb (value);
        updatePreview();
    };

    pivotStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        plugin.setViewProperty (FXParams::propSpectrumTiltPivot,
                                juce::jlimit (0, FXParams::spectrumTiltPivotNames.size() - 1, index));
        updatePreview();
    };

    reactivityStepper.onChange = [this] (int index)
    {
        if (! syncing)
            plugin.setChoiceParameter (FXParams::reactivity, index);
    };

    attackStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        const auto clamped = juce::jlimit (0, FXParams::spectrumAttackNames.size() - 1, index);

        plugin.setViewProperty (FXParams::propSpectrumAttack, clamped);
        plugin.getAnalysis().setSpectrumAttackSeconds (FXParams::spectrumAttackValues[clamped]);
    };

    releaseStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        const auto clamped = juce::jlimit (0, FXParams::spectrumReleaseNames.size() - 1, index);

        plugin.setViewProperty (FXParams::propSpectrumRelease, clamped);
        plugin.getAnalysis().setSpectrumReleaseSeconds (FXParams::spectrumReleaseValues[clamped]);
    };

    channelStepper.onChange = [this] (int index)
    {
        if (! syncing)
            plugin.setChoiceParameter (FXParams::channel, index);
    };

    dcBlockStepper.onChange = [this] (int index)
    {
        if (! syncing)
            plugin.setBoolParameter (FXParams::dcBlock, index == 1);
    };

    inputGainStepper.onGestureStart = [this] { plugin.beginGesture (FXParams::inputGainDb); };
    inputGainStepper.onGestureEnd   = [this] { plugin.endGesture   (FXParams::inputGainDb); };

    inputGainStepper.onNumericChange = [this] (float value)
    {
        if (! syncing)
            plugin.setFloatParameter (FXParams::inputGainDb, value);
    };

    // The Spectrum page's own rule, kept: a sonogram carries history drawn on
    // one frequency scale, which is meaningless on another. The preview's
    // syncFromState() discards it on either change.
    viewStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        // The stepper counts in display order; the property stores the mode.
        plugin.setViewProperty (FXParams::propSpectrumMode, FXParams::spectrumModeFromStepIndex (index));
        updatePreview();
    };

    scaleStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        plugin.setViewProperty (FXParams::propSpectrumScale,
                                juce::jlimit (0, FXParams::frequencyScaleNames.size() - 1, index));
        updatePreview();
    };

    themeStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        const auto& themes = builtInThemes();
        plugin.setTheme (themes[(size_t) juce::jlimit (0, (int) themes.size() - 1, index)]);

        if (onThemeSelected != nullptr)
            onThemeSelected();
    };
}

std::vector<StepperControl*> SettingsPage::allSteppers()
{
    std::vector<StepperControl*> steppers;

    for (auto* row : { &topRow, &bottomRow })
        for (const auto& group : *row)
            steppers.insert (steppers.end(), group.controls.begin(), group.controls.end());

    return steppers;
}

//==============================================================================
void SettingsPage::themeChanged()
{
    // ThemedComponent already hands the theme to every child, the preview
    // included; the layout depends on the type sizes, so it is redone.
    resized();
}

void SettingsPage::pageShown()
{
    syncFromState();
    preview.pageShown();
    plugin.getAnalysis().setActivePage (FXParams::Page::settings);
}

void SettingsPage::refresh()
{
    syncFromState();
    preview.refresh();
}

void SettingsPage::updatePreview()
{
    preview.pageShown();
    preview.refresh();
}

void SettingsPage::syncFromState()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    const auto viewIndex = [this] (const char* id, int fallback, int count)
    {
        return juce::jlimit (0, count - 1, (int) plugin.getViewProperty (id, fallback));
    };

    resolutionStepper.setSelectedIndex (viewIndex (FXParams::propSpectrumResolution,
                                                   FXParams::defaultSpectrumResolutionIndex,
                                                   FXParams::spectrumResolutionNames.size()),
                                        juce::dontSendNotification);

    const auto order = (int) plugin.getViewProperty (FXParams::propFftOrder,
                                                     FXParams::fftOrderValues[FXParams::defaultFftOrderIndex]);

    for (int i = 0; i <= FXParams::lastFftSizeIndex; ++i)
        if (FXParams::fftOrderValues[i] == order)
            fftSizeStepper.setSelectedIndex (i, juce::dontSendNotification);

    bandsStepper.setSelectedIndex (viewIndex (FXParams::propSpectrumSmoothing,
                                              FXParams::defaultSpectrumSmoothingIndex,
                                              FXParams::spectrumSmoothingNames.size()),
                                   juce::dontSendNotification);

    topStepper.setSelectedIndex (viewIndex (FXParams::propSpectrumTop, FXParams::defaultSpectrumTopIndex,
                                            FXParams::spectrumTopNames.size()),
                                 juce::dontSendNotification);

    rangeStepper.setSelectedIndex (viewIndex (FXParams::propSpectrumRange, FXParams::defaultSpectrumRangeIndex,
                                              FXParams::spectrumRangeNames.size()),
                                   juce::dontSendNotification);

    slopeStepper.setNumericValue (plugin.getSpectrumTiltDb(), juce::dontSendNotification);

    pivotStepper.setSelectedIndex (viewIndex (FXParams::propSpectrumTiltPivot,
                                              FXParams::defaultSpectrumTiltPivotIndex,
                                              FXParams::spectrumTiltPivotNames.size()),
                                   juce::dontSendNotification);

    reactivityStepper.setSelectedIndex (plugin.getChoiceParameter (FXParams::reactivity), juce::dontSendNotification);

    attackStepper.setSelectedIndex (viewIndex (FXParams::propSpectrumAttack, FXParams::defaultSpectrumAttackIndex,
                                               FXParams::spectrumAttackNames.size()),
                                    juce::dontSendNotification);

    releaseStepper.setSelectedIndex (viewIndex (FXParams::propSpectrumRelease, FXParams::defaultSpectrumReleaseIndex,
                                                FXParams::spectrumReleaseNames.size()),
                                     juce::dontSendNotification);

    channelStepper.setSelectedIndex (plugin.getChoiceParameter (FXParams::channel), juce::dontSendNotification);
    dcBlockStepper.setSelectedIndex (plugin.getBoolParameter (FXParams::dcBlock) ? 1 : 0, juce::dontSendNotification);
    inputGainStepper.setNumericValue (plugin.getFloatParameter (FXParams::inputGainDb), juce::dontSendNotification);

    viewStepper.setSelectedIndex (FXParams::spectrumModeToStepIndex (
                                      viewIndex (FXParams::propSpectrumMode, 0, FXParams::lastSpectrumMode + 1)),
                                  juce::dontSendNotification);

    scaleStepper.setSelectedIndex (viewIndex (FXParams::propSpectrumScale, 0, FXParams::frequencyScaleNames.size()),
                                   juce::dontSendNotification);

    const auto themeName = plugin.getTheme().name;
    const auto& themes = builtInThemes();

    for (size_t i = 0; i < themes.size(); ++i)
        if (themes[i].name == themeName)
            themeStepper.setSelectedIndex ((int) i, juce::dontSendNotification);
}

//==============================================================================
void SettingsPage::paint (juce::Graphics& g)
{
    const auto& t = theme();

    g.setColour (t.background);
    g.fillRect (getLocalBounds());

    // Group titles with a hairline under each, spanning the group. The rule is
    // what makes three steppers read as one group rather than as three
    // neighbours, and it is drawn in the dim text colour rather than the grid
    // colour because several themes set the grid so faint that it would carry
    // the grouping only on the default one.
    g.setFont (Theme::font (t.axisSize, juce::Font::bold));

    for (auto* row : { &topRow, &bottomRow })
    {
        for (const auto& group : *row)
        {
            if (group.titleArea.isEmpty())
                continue;

            g.setColour (t.dimText());
            g.drawText (group.title, group.titleArea.withTrimmedLeft (4),
                        juce::Justification::centredLeft, false);

            g.setColour (t.dimText().withMultipliedAlpha (0.55f));
            g.fillRect (group.titleArea.getX(), group.titleArea.getBottom() - 1,
                        group.titleArea.getWidth(), 1);
        }
    }
}

void SettingsPage::resized()
{
    auto area = getLocalBounds().reduced (16, 8);

    // Every stepper is the same kind, so one preferred height serves them all.
    const auto stepperHeight = resolutionStepper.getPreferredHeight();
    const auto titleHeight   = juce::roundToInt (theme().axisSize * 1.7f);
    const auto rowHeight     = titleHeight + 2 + stepperHeight;
    const auto rowGap        = 6;
    const auto groupGap      = 16;

    const auto controlsHeight = rowHeight * 2 + rowGap;

    // The preview gets what is left, and gives it up first. Below a readable
    // height it is hidden rather than squeezed: a graph thirty points tall has
    // no curve in it anyone could tune against, and the controls are the part
    // of this page that must survive a small window.
    const auto previewHeight = area.getHeight() - controlsHeight - 8;
    const auto showPreview   = previewHeight >= 90;

    preview.setVisible (showPreview);

    if (showPreview)
    {
        preview.setBounds (area.removeFromTop (previewHeight).expanded (6, 0));
        area.removeFromTop (8);
    }
    else
    {
        // Centre the controls in the height they are given instead.
        area = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), controlsHeight));
    }

    // Both rows hold eight controls in groups of 3-2-3, so the columns are one
    // width across the page, a stepper in the upper row sits over one in the
    // lower row, and the two gaps between groups fall in the same place.
    const auto layOutRow = [&] (juce::Rectangle<int> row, std::vector<Group>& groups)
    {
        int columns = 0;

        for (const auto& group : groups)
            columns += (int) group.controls.size();

        if (columns == 0)
            return;

        const auto gaps        = (int) groups.size() - 1;
        const auto columnWidth = (row.getWidth() - gaps * groupGap) / columns;

        for (auto& group : groups)
        {
            auto groupArea = row.removeFromLeft (columnWidth * (int) group.controls.size());
            row.removeFromLeft (groupGap);

            group.titleArea = groupArea.removeFromTop (titleHeight);
            groupArea.removeFromTop (2);

            for (auto* control : group.controls)
                control->setBounds (groupArea.removeFromLeft (columnWidth)
                                             .withSizeKeepingCentre (columnWidth - 6, stepperHeight));
        }
    };

    layOutRow (area.removeFromTop (rowHeight), topRow);
    area.removeFromTop (rowGap);
    layOutRow (area.removeFromTop (rowHeight), bottomRow);

    repaint();
}
