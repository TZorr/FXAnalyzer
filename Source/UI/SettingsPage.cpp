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
        { "RESOLUTION", { &resolutionStepper, &fftSizeStepper, &bandsStepper }, {}, {} },
        { "RANGE",      { &topStepper, &rangeStepper },                          {}, {} },
        { "DISPLAY",    { &viewStepper, &scaleStepper },                         {}, {} }
    };

    bottomRow =
    {
        { "SMOOTHING",  { &reactivityStepper, &attackStepper, &releaseStepper }, {}, {} },
        { "TILT",       { &slopeKnob, &pivotStepper },                           {}, {} },
        { "INPUT",      { &channelStepper, &dcBlockStepper, &inputGainKnob },    {}, {} }
    };

    // Compact, all of them. See the file header for why the large size had to go.
    for (auto* stepper : allSteppers())
        stepper->setCompact (true);

    for (auto* row : { &topRow, &bottomRow })
        for (const auto& group : *row)
            for (auto* control : group.controls)
                addAndMakeVisible (*control);

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
    // A knob since 0.2: the one continuous display setting. Snapped to the
    // half-decibel steps the stepper took, which is as finely as a tilt is
    // worth choosing; double-click returns to the default.
    slopeKnob.setRange (FXParams::spectrumTiltMinDb, FXParams::spectrumTiltMaxDb, FXParams::spectrumTiltStepDb);
    slopeKnob.setDoubleClickReturnValue (true, FXParams::defaultSpectrumTiltDb());
    slopeKnob.textFromValueFunction = [] (double db) { return SpectrumPage::formatTilt ((float) db); };

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

    // A knob since 0.2, bipolar from unity. Continuous, to a tenth of a
    // decibel, because it is an automatable parameter and quantising it would
    // snap every automation curve to the steps. Double-click returns to 0 dB.
    inputGainKnob.setRange (FXParams::inputGainMinDb, FXParams::inputGainMaxDb, 0.1);
    inputGainKnob.setDoubleClickReturnValue (true, 0.0);
    inputGainKnob.textFromValueFunction = [] (double value)
    {
        const auto db = (float) value;
        const auto rounded = std::abs (db) < 0.05f ? 0.0f : db;

        if (std::abs (rounded - std::round (rounded)) < 0.05f)
            return rounded == 0.0f ? juce::String ("0 dB")
                                   : (rounded > 0.0f ? "+" : "") + juce::String ((int) std::round (rounded)) + " dB";

        return (rounded > 0.0f ? "+" : "") + juce::String (rounded, 1) + " dB";
    };

    // ---- DISPLAY ------------------------------------------------------------
    viewStepper.setLabel ("View");
    viewStepper.setItems (FXParams::spectrumModeNames);

    scaleStepper.setLabel ("Scale");
    scaleStepper.setItems (FXParams::frequencyScaleNames);

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

    slopeKnob.onValueChange = [this]
    {
        if (syncing)
            return;

        plugin.setSpectrumTiltDb ((float) slopeKnob.getValue());
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

    // A drag, a wheel step and a double-click all arrive between these two, so
    // the host sees each as one gesture.
    inputGainKnob.onDragStart = [this] { plugin.beginGesture (FXParams::inputGainDb); };
    inputGainKnob.onDragEnd   = [this] { plugin.endGesture   (FXParams::inputGainDb); };

    inputGainKnob.onValueChange = [this]
    {
        if (! syncing)
            plugin.setFloatParameter (FXParams::inputGainDb, (float) inputGainKnob.getValue());
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

}

std::vector<StepperControl*> SettingsPage::allSteppers()
{
    std::vector<StepperControl*> steppers;

    for (auto* row : { &topRow, &bottomRow })
        for (const auto& group : *row)
            for (auto* control : group.controls)
                if (auto* stepper = dynamic_cast<StepperControl*> (control))
                    steppers.push_back (stepper);

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

    if (! slopeKnob.isMouseButtonDown())
        slopeKnob.setValue (plugin.getSpectrumTiltDb(), juce::dontSendNotification);

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

    if (! inputGainKnob.isMouseButtonDown())
        inputGainKnob.setValue (plugin.getFloatParameter (FXParams::inputGainDb), juce::dontSendNotification);

    viewStepper.setSelectedIndex (FXParams::spectrumModeToStepIndex (
                                      viewIndex (FXParams::propSpectrumMode, 0, FXParams::lastSpectrumMode + 1)),
                                  juce::dontSendNotification);

    scaleStepper.setSelectedIndex (viewIndex (FXParams::propSpectrumScale, 0, FXParams::frequencyScaleNames.size()),
                                   juce::dontSendNotification);
}

//==============================================================================
void SettingsPage::paint (juce::Graphics& g)
{
    const auto& t = theme();

    g.setColour (t.background);
    g.fillRect (getLocalBounds());

    // Kitbox's sections: a recessed panel per group, its name in the top left.
    // The panel is what makes three controls read as one group rather than as
    // three neighbours.
    for (auto* row : { &topRow, &bottomRow })
    {
        for (const auto& group : *row)
        {
            if (group.sectionArea.isEmpty())
                continue;

            g.setColour (t.section);
            g.fillRoundedRectangle (group.sectionArea.toFloat(), 6.0f);

            g.setColour (t.text);
            g.setFont (Theme::labelFontAt (10.0f));
            g.drawText (group.title, group.titleArea, juce::Justification::centredLeft, false);
        }
    }
}

void SettingsPage::resized()
{
    auto area = getLocalBounds().reduced (Layout::pageMarginX, Layout::pageMarginY);

    const auto titleHeight = 24;

    // 88 where there is room, 78 in a small window: the ten points a row gives
    // up there are what keep the preview above its 90 point floor at three
    // quarters size, and the preview is worth more than ten points of knob.
    const auto rowHeight   = area.getHeight() >= 2 * 88 + 8 + 10 + 90 ? 88 : 78;
    const auto rowGap      = 8;
    const auto groupGap    = 10;

    const auto controlsHeight = rowHeight * 2 + rowGap;

    // The preview gets what is left, and gives it up first. Below a readable
    // height it is hidden rather than squeezed: a graph thirty points tall has
    // no curve in it anyone could tune against, and the controls are the part
    // of this page that must survive a small window.
    const auto previewHeight = area.getHeight() - controlsHeight - 10;
    const auto showPreview   = previewHeight >= 90;

    preview.setVisible (showPreview);

    if (showPreview)
    {
        // The preview is a SpectrumPage and keeps the page margin itself, so
        // it is given the margin back to draw its display where this area is.
        preview.setBounds (area.removeFromTop (previewHeight).expanded (Layout::pageMarginX, Layout::pageMarginY));
        area.removeFromTop (10);
    }
    else
    {
        // Centre the controls in the height they are given instead.
        area = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), controlsHeight));
    }

    // Both rows are cut into the same eight columns, grouped 3-2-3, so a
    // control in the upper row sits over one in the lower row and the gaps
    // between sections fall in the same place. Display has two controls since
    // the theme went; its section keeps the three-column width anyway.
    static constexpr int columnsPerGroup[] { 3, 2, 3 };

    const auto layOutRow = [&] (juce::Rectangle<int> row, std::vector<Group>& groups)
    {
        const auto columnWidth = (row.getWidth() - 2 * groupGap) / 8;

        for (size_t i = 0; i < groups.size(); ++i)
        {
            auto& group = groups[i];
            const auto columns = columnsPerGroup[juce::jmin ((size_t) 2, i)];

            auto section = i + 1 == groups.size() ? row : row.removeFromLeft (columnWidth * columns);
            row.removeFromLeft (groupGap);

            group.sectionArea = section;

            auto inner = section.reduced (6, 0);
            group.titleArea = inner.removeFromTop (titleHeight).withTrimmedLeft (4);
            inner.removeFromBottom (2);

            for (auto* control : group.controls)
            {
                auto cell = inner.removeFromLeft (columnWidth).reduced (4, 0);

                if (auto* stepper = dynamic_cast<StepperControl*> (control))
                    stepper->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), stepper->getPreferredHeight()));
                else
                    control->setBounds (cell);
            }
        }
    };

    layOutRow (area.removeFromTop (rowHeight), topRow);
    area.removeFromTop (rowGap);
    layOutRow (area.removeFromTop (rowHeight), bottomRow);

    repaint();
}
