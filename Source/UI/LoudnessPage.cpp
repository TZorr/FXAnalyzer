//
//  LoudnessPage.cpp
//  FX Analyzer
//

#include "LoudnessPage.h"
#include "../PluginProcessor.h"

#include <cmath>

namespace
{
    /** A LUFS or dBTP value as the panel writes it. The silence sentinel gets a
        dash rather than "-200.0", because -200 looks like a measurement and a
        dash looks like what it is: nothing measured yet. */
    juce::String formatLoudness (float value)
    {
        return value <= -190.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"))
                                : juce::String (value, 1);
    }
}

//==============================================================================
LoudnessPage::LoudnessPage (FXAnalyzerProcessor& processor) : PageBase (processor)
{
    addAndMakeVisible (targetStepper);
    addAndMakeVisible (resetButton);

    targetStepper.setCompact (true);
    targetStepper.setLabel ("Target");
    targetStepper.setItems (FXParams::loudnessTargetNames);

    resetButton.setToggleMode (false);
    resetButton.setTooltip ("Reset integrated loudness, range and held true peak");

    targetStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        targetIndex = index;
        plugin.setViewProperty (FXParams::propLoudnessTarget, index);
        repaint();
    };

    resetButton.onClick = [this] (bool) { plugin.getAnalysis().resetMeters(); };
}

void LoudnessPage::themeChanged()
{
}

void LoudnessPage::pageShown()
{
    syncFromState();
    plugin.getAnalysis().setActivePage (FXParams::Page::loudness);
}

void LoudnessPage::syncFromState()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    targetIndex = juce::jlimit (0, 3, (int) plugin.getViewProperty (FXParams::propLoudnessTarget,
                                                                    FXParams::defaultLoudnessTargetIndex));
    targetStepper.setSelectedIndex (targetIndex, juce::dontSendNotification);
}

void LoudnessPage::refresh()
{
    repaint();
}

//==============================================================================
void LoudnessPage::resized()
{
    auto area = getLocalBounds().reduced (10, 10);
    auto sideColumn = area.removeFromRight (Layout::sideColumnWidth).reduced (6, 0);

    layOutStepperColumn (sideColumn.removeFromTop (sideColumn.getHeight() / 3), { &targetStepper });
    sideColumn.removeFromTop (14);
    resetButton.setBounds (sideColumn.removeFromTop (40).withSizeKeepingCentre (36, 36));
}

//==============================================================================
void LoudnessPage::paint (juce::Graphics& g)
{
    const auto& t = theme();

    g.setColour (t.background);
    g.fillRect (getLocalBounds());

    auto area = getLocalBounds().reduced (10, 10);
    area.removeFromRight (Layout::sideColumnWidth);

    const auto& loudness = plugin.getAnalysis().getLoudness();
    const auto& truePeak = plugin.getAnalysis().getTruePeak();

    const auto momentary  = loudness.getMomentaryLufs();
    const auto shortTerm  = loudness.getShortTermLufs();
    const auto integrated = loudness.getIntegratedLufs();
    const auto peakDb     = truePeak.getMaxTruePeakDb();
    const auto settled    = loudness.hasIntegratedFor (FXParams::integratedSettleSeconds);
    const auto target     = FXParams::loudnessTargetValues[juce::jlimit (0, 3, targetIndex)];

    // ---- The bars -------------------------------------------------------
    auto barRow = area.removeFromLeft (juce::roundToInt ((float) area.getWidth() * 0.38f)).toFloat();
    barRow = barRow.reduced (6.0f, 4.0f);

    const auto barWidth = barRow.getWidth() / 3.0f;

    const juce::String barCaptions[] { "M", "S", "I" };
    const float barValues[] { momentary, shortTerm, integrated };
    const juce::Colour barColours[] { t.curve, t.curveAlt, settled ? t.curve : t.accentDim() };

    for (int i = 0; i < 3; ++i)
    {
        auto column = barRow.withWidth (barWidth).translated (barWidth * (float) i, 0.0f).reduced (10.0f, 0.0f);
        auto caption = column.removeFromBottom (22.0f);

        paintBar (g, column, barValues[i], barColours[i]);

        g.setColour (t.accent);
        g.setFont (t.labelFont());
        g.drawText (barCaptions[i], caption, juce::Justification::centred, false);
    }

    // The target line runs across all three bars, because the question is
    // whether this material is above or below it, and that is easier to see as
    // one line than as three tick marks.
    {
        const auto proportion = juce::jlimit (0.0f, 1.0f,
                                              (scaleTopLufs - target) / (scaleTopLufs - scaleBottomLufs));
        const auto y = barRow.getY() + proportion * (barRow.getHeight() - 22.0f);

        g.setColour (t.warning.withAlpha (0.8f));
        g.fillRect (barRow.getX(), y, barRow.getWidth(), 1.5f);

        g.setFont (t.axisFont());
        g.drawText (FXParams::loudnessTargetNames[juce::jlimit (0, 3, targetIndex)],
                    juce::Rectangle<float> (barRow.getX(), y - 18.0f, barRow.getWidth() - 4.0f, 16.0f),
                    juce::Justification::centredRight, false);
    }

    // ---- The numbers ----------------------------------------------------
    auto numbers = area.reduced (14, 4);

    auto topHalf = numbers.removeFromTop (juce::roundToInt ((float) numbers.getHeight() * 0.52f));

    // The distance to target is the number anybody acts on, so it sits directly
    // under the integrated value rather than floating between the two rows.
    auto targetLine = topHalf.removeFromBottom (20);

    paintReadout (g, topHalf.toFloat(), "INTEGRATED",
                  formatLoudness (integrated), "LUFS",
                  46.0f, settled ? t.text : t.dimText());

    // A gap, so the target line belongs to the integrated block above it rather
    // than reading as a caption for the row below.
    numbers.removeFromTop (12);

    const auto columnWidth = numbers.getWidth() / 2;

    auto leftColumn  = numbers.removeFromLeft (columnWidth);
    auto rightColumn = numbers;

    auto momentaryArea = leftColumn.removeFromTop (leftColumn.getHeight() / 2);
    auto rangeArea     = leftColumn;
    auto shortArea     = rightColumn.removeFromTop (rightColumn.getHeight() / 2);
    auto peakArea      = rightColumn;

    paintReadout (g, momentaryArea.toFloat(), "MOMENTARY",  formatLoudness (momentary), "LUFS", 24.0f, t.text);
    paintReadout (g, shortArea.toFloat(),     "SHORT TERM", formatLoudness (shortTerm), "LUFS", 24.0f, t.text);

    // Loudness range gates itself inside the meter - it reports silence until
    // it has a distribution rather than a handful of values - so the page just
    // shows what it is given and dims the dash.
    const auto lra = loudness.getLoudnessRange();
    const auto haveRange = lra > -190.0f;

    paintReadout (g, rangeArea.toFloat(), "RANGE",
                  haveRange ? juce::String (lra, 1) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")),
                  haveRange ? "LU" : "", 24.0f, haveRange ? t.text : t.dimText());

    // True peak turns red at the point it starts to matter, which is -1 dBTP
    // rather than 0: that is the ceiling every streaming platform's encoder
    // needs, and a master that reads -0.3 dBTP will clip on their side.
    const auto peakColour = peakDb > -1.0f ? t.warning : t.text;
    paintReadout (g, peakArea.toFloat(), "TRUE PEAK", formatLoudness (peakDb), "dBTP", 24.0f, peakColour);

    // ---- Distance to target ---------------------------------------------
    if (settled)
    {
        const auto difference = integrated - target;
        const auto text = (difference >= 0.0f ? "+" : "") + juce::String (difference, 1) + " LU to target";

        g.setColour (std::abs (difference) < 0.5f ? t.curve : t.dimText());
        g.setFont (t.labelFont());
        g.drawText (text, targetLine.toFloat(), juce::Justification::centredLeft, false);
    }
}

//==============================================================================
void LoudnessPage::paintBar (juce::Graphics& g, juce::Rectangle<float> area, float lufs, juce::Colour colour) const
{
    const auto& t = theme();

    g.setColour (t.gridMinor());
    g.fillRoundedRectangle (area, 3.0f);

    if (lufs > -190.0f)
    {
        const auto proportion = juce::jlimit (0.0f, 1.0f,
                                              (lufs - scaleBottomLufs) / (scaleTopLufs - scaleBottomLufs));

        auto filled = area.withTop (area.getBottom() - area.getHeight() * proportion);

        g.setColour (colour);
        g.fillRoundedRectangle (filled, 3.0f);
    }

    g.setColour (t.grid);
    g.drawRoundedRectangle (area, 3.0f, 1.0f);
}

void LoudnessPage::paintReadout (juce::Graphics& g, juce::Rectangle<float> area,
                                 const juce::String& caption, const juce::String& value,
                                 const juce::String& unit, float textSize, juce::Colour colour) const
{
    const auto& t = theme();

    auto captionArea = area.removeFromTop (juce::jmin (20.0f, area.getHeight() * 0.3f));

    g.setColour (t.accent);
    g.setFont (t.labelFont());
    g.drawText (caption, captionArea, juce::Justification::centredLeft, false);

    const auto numberFont = t.numberFont (juce::jmin (textSize, area.getHeight() * 0.8f));
    const auto unitFont   = t.font (juce::jmin (textSize * 0.5f, 20.0f), juce::Font::plain);

    g.setColour (colour);
    g.setFont (numberFont);
    g.drawText (value, area, juce::Justification::centredLeft, false);

    if (unit.isNotEmpty())
    {
        const auto numberWidth = juce::GlyphArrangement::getStringWidth (numberFont, value);

        g.setColour (t.dimText());
        g.setFont (unitFont);
        g.drawText (unit, area.withTrimmedLeft (numberWidth + 8.0f),
                    juce::Justification::centredLeft, false);
    }
}
