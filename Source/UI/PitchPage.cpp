//
//  PitchPage.cpp
//  FX Analyzer
//

#include "PitchPage.h"
#include "GraphAxes.h"
#include "../PluginProcessor.h"

#include <cmath>

//==============================================================================
PitchPage::PitchPage (FXAnalyzerProcessor& processor) : PageBase (processor)
{
    addAndMakeVisible (referenceStepper);

    // 415 to 466 Hz in whole hertz. Whole hertz because that is the resolution
    // every other tuner and every orchestra quotes concert pitch in, and a
    // stepper offering 440.5 would invite a precision nobody works to.
    for (int hz = (int) FXParams::pitchRefMinHz; hz <= (int) FXParams::pitchRefMaxHz; ++hz)
        referenceNames.add (juce::String (hz) + " Hz");

    // "Ref A4", not "Reference": the column is 108 points wide and the label
    // has to say which A it means, which is the half of the phrase that
    // carries the information.
    referenceStepper.setCompact (true);
    referenceStepper.setLabel ("Ref A4");
    referenceStepper.setItems (referenceNames);

    referenceStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        referenceIndex = index;

        const auto hz = FXParams::pitchRefMinHz + (float) index;
        plugin.setViewProperty (FXParams::propPitchRefHz, hz);
        plugin.getAnalysis().setPitchReferenceHz (hz);
        repaint();
    };
}

void PitchPage::themeChanged()
{
}

void PitchPage::pageShown()
{
    syncFromState();
    plugin.getAnalysis().setActivePage (FXParams::Page::pitch);
}

void PitchPage::syncFromState()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    const auto hz = (float) plugin.getViewProperty (FXParams::propPitchRefHz, FXParams::pitchRefDefaultHz);

    referenceIndex = juce::jlimit (0, referenceNames.size() - 1,
                                   juce::roundToInt (hz - FXParams::pitchRefMinHz));

    referenceStepper.setSelectedIndex (referenceIndex, juce::dontSendNotification);
    plugin.getAnalysis().setPitchReferenceHz (FXParams::pitchRefMinHz + (float) referenceIndex);
}

void PitchPage::refresh()
{
    repaint();
}

//==============================================================================
void PitchPage::resized()
{
    auto area = getLocalBounds().reduced (Layout::pageMarginX, Layout::pageMarginY);
    auto sideColumn = area.removeFromRight (Layout::sideColumnWidth).withTrimmedLeft (10);

    layOutStepperColumn (sideColumn.removeFromTop (sideColumn.getHeight() / 3), { &referenceStepper });
}

void PitchPage::paint (juce::Graphics& g)
{
    const auto& t = theme();

    g.setColour (t.background);
    g.fillRect (getLocalBounds());

    auto area = getLocalBounds().reduced (Layout::pageMarginX, Layout::pageMarginY);
    area.removeFromRight (Layout::sideColumnWidth);

    GraphAxes::paintDisplay (g, t, area.toFloat());

    area = area.reduced (40, 20);

    const auto result = plugin.getAnalysis().getPitch();
    const auto reference = plugin.getAnalysis().getPitchReferenceHz();
    const auto haveNote = result.valid && result.confidence >= FXParams::pitchMinConfidence;
    const auto note = NoteName::fromFrequency (result.frequencyHz, reference);

    // ---- The note -------------------------------------------------------
    auto noteArea = area.removeFromTop (juce::roundToInt ((float) area.getHeight() * 0.46f)).toFloat();

    g.setColour (haveNote ? t.bedText : t.bedDim);
    g.setFont (Theme::numberFont (juce::jmin (104.0f, noteArea.getHeight() * 0.86f), true));
    g.drawText (haveNote ? note.name + juce::String (note.octave)
                         : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")),
                noteArea, juce::Justification::centred, false);

    // ---- Cents ----------------------------------------------------------
    auto centsArea = area.removeFromTop (juce::roundToInt ((float) area.getHeight() * 0.42f)).toFloat();

    paintCentsBar (g, centsArea, note.cents, haveNote);

    // ---- The numbers underneath -----------------------------------------
    auto footer = area.toFloat();
    const auto columnWidth = footer.getWidth() / 3.0f;

    const auto column = [&] (int index, const juce::String& caption, const juce::String& value, juce::Colour colour)
    {
        auto bounds = footer.withWidth (columnWidth).translated (columnWidth * (float) index, 0.0f);
        auto captionArea = bounds.removeFromTop (16.0f);

        g.setColour (t.bedDim);
        g.setFont (t.axisFont().withHeight (10.0f));
        g.drawText (caption, captionArea, juce::Justification::centred, false);

        g.setColour (colour);
        g.setFont (Theme::numberFont (20.0f, true));
        g.drawText (value, bounds, juce::Justification::centred, false);
    };

    const auto dash = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"));

    column (0, "FREQUENCY",
            haveNote ? juce::String (result.frequencyHz, result.frequencyHz < 1000.0f ? 2 : 1) + " Hz" : dash,
            haveNote ? t.bedText : t.bedDim);

    column (1, "CENTS",
            haveNote ? (note.cents >= 0.0f ? "+" : "") + juce::String (note.cents, 1) : dash,
            haveNote ? t.bedText : t.bedDim);

    // Confidence is coloured by the same threshold the note display uses, so
    // the two never disagree about whether there is a note.
    column (2, "CONFIDENCE",
            juce::String (juce::roundToInt (result.confidence * 100.0f)) + "%",
            haveNote ? t.bedText : t.bedDim);
}

void PitchPage::paintCentsBar (juce::Graphics& g, juce::Rectangle<float> area, float cents, bool haveNote) const
{
    const auto& t = theme();

    auto bar = area.withSizeKeepingCentre (area.getWidth(), 26.0f);

    g.setColour (t.grid);
    g.fillRoundedRectangle (bar, 4.0f);

    // Tick marks every ten cents, with the centre band drawn as a block rather
    // than a line: the target is a region, and a hairline invites chasing a
    // precision the detector does not have.
    const auto centreX = bar.getCentreX();
    const auto pixelsPerCent = bar.getWidth() / 100.0f;

    g.setColour (t.bedButton);
    g.fillRect (centreX - 5.0f * pixelsPerCent, bar.getY(), 10.0f * pixelsPerCent, bar.getHeight());

    g.setColour (t.bedDim.withAlpha (0.6f));

    for (int tick = -40; tick <= 40; tick += 10)
        if (tick != 0)
            g.fillRect (centreX + (float) tick * pixelsPerCent, bar.getY() + 6.0f, 1.0f, bar.getHeight() - 12.0f);

    g.setColour (t.bedDim);
    g.fillRect (centreX - 0.5f, bar.getY(), 1.0f, bar.getHeight());

    if (haveNote)
    {
        const auto clamped = juce::jlimit (-50.0f, 50.0f, cents);
        const auto inTune  = std::abs (cents) <= 5.0f;

        const auto marker = juce::Rectangle<float> (5.0f, bar.getHeight() + 8.0f)
                                .withCentre ({ centreX + clamped * pixelsPerCent, bar.getCentreY() });

        g.setColour (inTune ? t.curve : t.warning);
        g.fillRoundedRectangle (marker, 2.5f);
    }

    g.setColour (t.bedDim);
    g.setFont (t.axisFont());

    auto labels = area.removeFromBottom (16.0f);
    g.drawText ("-50", labels, juce::Justification::centredLeft, false);
    g.drawText ("+50", labels, juce::Justification::centredRight, false);
}
