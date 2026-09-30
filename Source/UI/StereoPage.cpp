//
//  StereoPage.cpp
//  FX Analyzer
//

#include "StereoPage.h"
#include "GraphAxes.h"
#include "../PluginProcessor.h"

#include <cmath>

//==============================================================================
StereoPage::StereoPage (FXAnalyzerProcessor& processor) : PageBase (processor)
{
    addAndMakeVisible (modeStepper);
    addAndMakeVisible (zoomStepper);

    for (auto* stepper : { &modeStepper, &zoomStepper })
        stepper->setCompact (true);

    modeStepper.setLabel ("Mode");
    modeStepper.setItems (FXParams::stereoModeNames);

    zoomStepper.setLabel ("Zoom");
    zoomStepper.setItems (FXParams::stereoZoomNames);

    modeStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        modeIndex = index;
        plugin.setViewProperty (FXParams::propStereoMode, index);
        repaint();
    };

    zoomStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        zoomIndex = index;
        plugin.setViewProperty (FXParams::propStereoZoomDb, index);
        repaint();
    };

    leftSamples.assign  ((size_t) cloudSize, 0.0f);
    rightSamples.assign ((size_t) cloudSize, 0.0f);
}

void StereoPage::themeChanged()
{
}

void StereoPage::pageShown()
{
    syncFromState();
    plugin.getAnalysis().setActivePage (FXParams::Page::stereo);
}

void StereoPage::syncFromState()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    modeIndex = juce::jlimit (0, 1, (int) plugin.getViewProperty (FXParams::propStereoMode, 0));
    zoomIndex = juce::jlimit (0, 3, (int) plugin.getViewProperty (FXParams::propStereoZoomDb, 0));

    modeStepper.setSelectedIndex (modeIndex, juce::dontSendNotification);
    zoomStepper.setSelectedIndex (zoomIndex, juce::dontSendNotification);
}

//==============================================================================
void StereoPage::refresh()
{
    if (! plugin.getAnalysis().isFrozen())
        collectPoints();

    repaint();
}

void StereoPage::collectPoints()
{
    const auto& ring = plugin.getAnalysis().getRing();

    ring.readLatest (AnalysisHub::ringLeft,  leftSamples.data(),  cloudSize);
    ring.readLatest (AnalysisHub::ringRight, rightSamples.data(), cloudSize);

    collected = cloudSize;
}

//==============================================================================
void StereoPage::resized()
{
    auto area = getLocalBounds().reduced (Layout::pageMarginX, Layout::pageMarginY);
    auto sideColumn = area.removeFromRight (Layout::sideColumnWidth).withTrimmedLeft (10);

    layOutStepperColumn (sideColumn, { &modeStepper, &zoomStepper });
}

void StereoPage::paint (juce::Graphics& g)
{
    const auto& t = theme();

    g.setColour (t.background);
    g.fillRect (getLocalBounds());

    auto area = getLocalBounds().reduced (Layout::pageMarginX, Layout::pageMarginY);
    area.removeFromRight (Layout::sideColumnWidth);

    // The goniometer is square, in a display of its own; whatever is left over
    // is a second display carrying the numbers.
    const auto size = juce::jmin (area.getHeight(), area.getWidth() / 2);
    auto scopeDisplay = area.removeFromLeft (size).withSizeKeepingCentre (size, size).toFloat();
    area.removeFromLeft (10);

    GraphAxes::paintDisplay (g, t, scopeDisplay);
    GraphAxes::paintDisplay (g, t, area.toFloat());

    paintGoniometer (g, scopeDisplay.reduced (8.0f));

    auto right = area.reduced (20, 14);
    auto correlationArea = right.removeFromTop (juce::roundToInt ((float) right.getHeight() * 0.34f)).toFloat();

    paintCorrelation (g, correlationArea);
    paintReadouts (g, right.toFloat());
}

//==============================================================================
void StereoPage::paintGoniometer (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& t = theme();

    const auto centre = area.getCentre();
    const auto radius = area.getWidth() * 0.5f - 6.0f;

    g.setColour (t.gridMinor());
    g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 1.0f);
    g.drawEllipse (juce::Rectangle<float> (radius, radius).withCentre (centre), 1.0f);

    // The four guide lines are the axes of the rotated frame: vertical is mono,
    // horizontal is pure side, and the diagonals are the two channels on their
    // own. Labelling them is what turns a cloud into a reading.
    g.setColour (t.grid);
    g.fillRect (centre.x - 0.5f, centre.y - radius, 1.0f, radius * 2.0f);
    g.fillRect (centre.x - radius, centre.y - 0.5f, radius * 2.0f, 1.0f);

    // The two diagonals are where a signal in one channel alone lands. Without
    // them the L and R labels in the corners are pointing at nothing, and a
    // reader has to work out from first principles which way a hard-panned
    // source leans - which is the one thing this display should not require.
    const auto diagonal = radius * 0.70710678f;

    g.setColour (t.gridMinor());
    g.drawLine (centre.x - diagonal, centre.y - diagonal, centre.x + diagonal, centre.y + diagonal, 1.0f);
    g.drawLine (centre.x - diagonal, centre.y + diagonal, centre.x + diagonal, centre.y - diagonal, 1.0f);

    g.setColour (t.bedDim);
    g.setFont (t.axisFont());
    g.drawText ("M", juce::Rectangle<float> (40.0f, 16.0f).withCentre ({ centre.x, area.getY() + 10.0f }),
                juce::Justification::centred, false);
    g.drawText ("L", juce::Rectangle<float> (40.0f, 16.0f).withCentre ({ area.getX() + 14.0f, area.getY() + 22.0f }),
                juce::Justification::centred, false);
    g.drawText ("R", juce::Rectangle<float> (40.0f, 16.0f).withCentre ({ area.getRight() - 14.0f, area.getY() + 22.0f }),
                juce::Justification::centred, false);

    if (collected <= 0)
        return;

    const auto zoom = juce::Decibels::decibelsToGain (
        FXParams::stereoZoomValues[juce::jlimit (0, 3, zoomIndex)]);

    // The 45 degree rotation, written out: mid goes up, side goes across, and
    // the 1/sqrt(2) keeps a full-scale mono signal on the circle rather than
    // sqrt(2) outside it.
    constexpr float invRoot2 = 0.70710678f;

    const auto pointFor = [&] (int index)
    {
        const auto l = leftSamples[(size_t) index]  * zoom;
        const auto r = rightSamples[(size_t) index] * zoom;

        const auto mid  = (l + r) * invRoot2;
        const auto side = (l - r) * invRoot2;

        return juce::Point<float> (centre.x + side * radius, centre.y - mid * radius);
    };

    const juce::Graphics::ScopedSaveState clipToScope (g);
    g.reduceClipRegion (area.toNearestInt());

    if (modeIndex == 1)
    {
        juce::Path path;
        path.startNewSubPath (pointFor (0));

        for (int i = 1; i < collected; ++i)
            path.lineTo (pointFor (i));

        g.setColour (t.curve.withAlpha (0.55f));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }
    else
    {
        // Rectangles rather than a path of dots: four thousand one-pixel
        // fillRect calls are noticeably cheaper than four thousand subpaths,
        // and at this size a dot is one pixel anyway.
        g.setColour (t.curve.withAlpha (0.5f));

        for (int i = 0; i < collected; ++i)
        {
            const auto point = pointFor (i);
            g.fillRect (point.x, point.y, 1.6f, 1.6f);
        }
    }
}

void StereoPage::paintCorrelation (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& t = theme();
    const auto correlation = plugin.getAnalysis().getStereo().getCorrelation();

    auto caption = area.removeFromTop (18.0f);

    g.setColour (t.bedDim);
    g.setFont (t.axisFont().withHeight (10.0f));
    g.drawText ("CORRELATION", caption, juce::Justification::centredLeft, false);

    auto bar = area.removeFromTop (22.0f);

    g.setColour (t.grid);
    g.fillRoundedRectangle (bar, 3.0f);

    const auto centreX = bar.getCentreX();
    const auto halfWidth = bar.getWidth() * 0.5f;
    const auto extent = correlation * halfWidth;

    // Below zero is drawn in the warning colour, because a negative correlation
    // on a music bus means the mono fold-down is going to lose something, and
    // that is the one state of this meter that requires action.
    g.setColour (correlation < 0.0f ? t.warning : t.curve);
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (centreX, centreX + extent), bar.getY(),
                                                    std::abs (extent), bar.getHeight()), 3.0f);

    g.setColour (t.bedDim);
    g.fillRect (centreX - 0.5f, bar.getY() - 2.0f, 1.0f, bar.getHeight() + 4.0f);

    auto scale = area.removeFromTop (18.0f);

    g.setColour (t.bedDim);
    g.setFont (t.axisFont());
    g.drawText ("-1", scale, juce::Justification::centredLeft, false);
    g.drawText ("0",  scale, juce::Justification::centred, false);
    g.drawText ("+1", scale, juce::Justification::centredRight, false);

    g.setColour (correlation < 0.0f ? t.warning : t.bedText);
    g.setFont (Theme::numberFont (26.0f, true));
    g.drawText (juce::String (correlation, 2), area, juce::Justification::centredLeft, false);
}

void StereoPage::paintReadouts (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& t = theme();
    const auto& analyser = plugin.getAnalysis().getStereo();

    const auto balance = analyser.getBalance();
    const auto width   = analyser.getWidthDb();

    const auto row = [&] (juce::Rectangle<float> bounds, const juce::String& caption, const juce::String& value)
    {
        auto captionArea = bounds.removeFromTop (16.0f);

        g.setColour (t.bedDim);
        g.setFont (t.axisFont().withHeight (10.0f));
        g.drawText (caption, captionArea, juce::Justification::centredLeft, false);

        g.setColour (t.bedText);
        g.setFont (Theme::numberFont (20.0f, true));
        g.drawText (value, bounds, juce::Justification::centredLeft, false);
    };

    const auto rowHeight = juce::jmin (56.0f, area.getHeight() / 3.0f);

    // Balance is written as a side, not as a signed number. "0.12" needs a
    // convention to read; "12% right" does not.
    const auto balanceText = std::abs (balance) < 0.02f
        ? juce::String ("Centred")
        : juce::String (juce::roundToInt (std::abs (balance) * 100.0f)) + "% "
              + (balance > 0.0f ? "right" : "left");

    row (area.removeFromTop (rowHeight), "BALANCE", balanceText);

    row (area.removeFromTop (rowHeight), "WIDTH",
         width <= -40.0f ? juce::String ("Mono") : juce::String (width, 1) + " dB S/M");

    g.setColour (t.bedDim);
    g.setFont (t.axisFont());
    g.drawText ("Measured on L and R, before the Channel selector",
                area.removeFromTop (18.0f), juce::Justification::centredLeft, false);
}
