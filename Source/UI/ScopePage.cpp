//
//  ScopePage.cpp
//  FX Analyzer
//

#include "ScopePage.h"
#include "../PluginProcessor.h"
#include "../TextUtf8.h"

#include <cmath>

//==============================================================================
ScopePage::ScopePage (FXAnalyzerProcessor& processor) : PageBase (processor)
{
    addAndMakeVisible (timeStepper);
    addAndMakeVisible (triggerStepper);
    addAndMakeVisible (gainStepper);
    addAndMakeVisible (freezeButton);

    for (auto* stepper : { &timeStepper, &triggerStepper, &gainStepper })
        stepper->setCompact (true);

    timeStepper.setLabel ("Time");
    timeStepper.setItems (FXParams::scopeTimeNames);

    triggerStepper.setLabel ("Trigger");
    triggerStepper.setItems (FXParams::scopeTriggerNames);

    gainStepper.setLabel ("Gain");
    gainStepper.setItems (FXParams::scopeGainNames);

    freezeButton.setTooltip ("Hold the display");

    timeStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        timeIndex = index;
        plugin.setViewProperty (FXParams::propScopeTimeMs, FXParams::scopeTimeValues[juce::jlimit (0, FXParams::lastScopeTimeIndex, index)]);
        applyTimeBase();
    };

    triggerStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        triggerIndex = index;
        plugin.setViewProperty (FXParams::propScopeTrigger, index);
        applyTimeBase();
    };

    gainStepper.onChange = [this] (int index)
    {
        if (syncing)
            return;

        gainIndex = index;
        plugin.setViewProperty (FXParams::propScopeGainDb, index);
        repaint();
    };

    freezeButton.onClick = [this] (bool state)
    {
        if (! syncing)
            plugin.setBoolParameter (FXParams::freeze, state);
    };
}

void ScopePage::themeChanged()
{

}

//==============================================================================
void ScopePage::pageShown()
{
    syncFromState();
    applyTimeBase();
    plugin.getAnalysis().setActivePage (FXParams::Page::scope);
}

void ScopePage::syncFromState()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    const auto milliseconds = (float) plugin.getViewProperty (
        FXParams::propScopeTimeMs, FXParams::scopeTimeValues[FXParams::defaultScopeTimeIndex]);

    timeIndex = FXParams::defaultScopeTimeIndex;

    for (int i = 0; i <= FXParams::lastScopeTimeIndex; ++i)
        if (std::abs (FXParams::scopeTimeValues[i] - milliseconds) < 0.01f)
            timeIndex = i;

    triggerIndex = juce::jlimit (0, FXParams::scopeTriggerNames.size() - 1,
                                 (int) plugin.getViewProperty (FXParams::propScopeTrigger,
                                                               FXParams::defaultScopeTriggerIndex));
    gainIndex    = juce::jlimit (0, 4, (int) plugin.getViewProperty (FXParams::propScopeGainDb,
                                                                     FXParams::defaultScopeGainIndex));

    timeStepper.setSelectedIndex (timeIndex, juce::dontSendNotification);
    triggerStepper.setSelectedIndex (triggerIndex, juce::dontSendNotification);
    gainStepper.setSelectedIndex (gainIndex, juce::dontSendNotification);
    freezeButton.setToggleState (plugin.getBoolParameter (FXParams::freeze));
}

void ScopePage::applyTimeBase()
{
    auto& analysis = plugin.getAnalysis();

    analysis.setScopeWindowSeconds (FXParams::scopeTimeValues[juce::jlimit (0, FXParams::lastScopeTimeIndex, timeIndex)] * 0.001f);
    analysis.setScopeTrigger ((FXParams::ScopeTrigger) juce::jlimit (0, FXParams::scopeTriggerNames.size() - 1, triggerIndex));
}

//==============================================================================
juce::Rectangle<float> ScopePage::displayArea() const
{
    auto area = getLocalBounds().reduced (Layout::pageMarginX, Layout::pageMarginY);
    area.removeFromRight (Layout::sideColumnWidth);

    return area.toFloat();
}

juce::Rectangle<float> ScopePage::plotArea() const
{
    return GraphAxes::plotInDisplay (displayArea());
}

void ScopePage::resized()
{
    auto area = getLocalBounds().reduced (Layout::pageMarginX, Layout::pageMarginY);
    auto sideColumn = area.removeFromRight (Layout::sideColumnWidth).withTrimmedLeft (10);

    layOutStepperColumn (sideColumn, { &timeStepper, &triggerStepper, &gainStepper });

    const auto plot = plotArea().toNearestInt();
    freezeButton.setBounds (plot.getRight() - 58, plot.getY() + 2, 58, 20);
}

void ScopePage::refresh()
{
    freezeButton.setToggleState (plugin.getBoolParameter (FXParams::freeze));
    repaint();
}

//==============================================================================
void ScopePage::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto plot = plotArea();

    g.setColour (t.background);
    g.fillRect (getLocalBounds());

    GraphAxes::paintDisplay (g, t, displayArea());
    GraphAxes::paintBed (g, t, plot);
    GraphAxes::paintDivisionGrid (g, t, plot, 10, 8);

    const auto& scope = plugin.getAnalysis().getScope();
    const auto count = scope.getNumSamples();

    // Auto gain aims the largest sample at 90% of half the height. Not 100%:
    // a trace that touches the frame on every peak cannot be told from one that
    // is clipping past it.
    const auto peak = juce::jmax (1.0e-4f, scope.getPeakMagnitude());

    // The stepper's entries are Auto, 0, +6, +12, +24 dB. Written out rather
    // than computed from the index: an expression that has to special-case its
    // own first entry is harder to check than five numbers.
    static constexpr float gainsDb[] { 0.0f, 0.0f, 6.0f, 12.0f, 24.0f };

    appliedGain = gainIndex == 0
        ? juce::jlimit (1.0f, 200.0f, 0.9f / peak)
        : juce::Decibels::decibelsToGain (gainsDb[juce::jlimit (0, 4, gainIndex)]);

    {
        const juce::Graphics::ScopedSaveState clipToPlot (g);
        g.reduceClipRegion (plot.toNearestInt());

        paintTrace (g, scope.getRight(), count, t.curveAlt.withAlpha (0.85f), appliedGain, 1.2f);
        paintTrace (g, scope.getLeft(),  count, t.curve, appliedGain, 1.6f);
    }

    // Axis labels: time across the bottom, amplitude down the side. The
    // amplitude axis is drawn in the units the gain is applied in, so it is
    // right whether the gain came from the stepper or from the auto setting.
    const auto milliseconds = FXParams::scopeTimeValues[juce::jlimit (0, FXParams::lastScopeTimeIndex, timeIndex)];

    g.setFont (t.axisFont());
    g.setColour (t.bedDim);

    for (int division = 0; division <= 10; division += 2)
    {
        const auto x = plot.getX() + plot.getWidth() * (float) division / 10.0f;
        const auto ms = milliseconds * (float) division / 10.0f;

        auto labelArea = juce::Rectangle<float> (60.0f, (float) Layout::axisGutterBottom - 4.0f)
                             .withCentre ({ x, plot.getBottom() + (float) Layout::axisGutterBottom * 0.5f - 2.0f });

        labelArea.setX (juce::jlimit (plot.getX(), plot.getRight() - 60.0f, labelArea.getX()));

        // Decimals chosen from the window, not from the individual label.
        // Deciding per label prints "4.0 ms" next to "12 ms", and a row of
        // numbers that cannot agree on its own format reads as a bug.
        g.drawText (juce::String (ms, milliseconds < 10.0f ? 2 : milliseconds < 100.0f ? 1 : 0) + " ms",
                    labelArea, juce::Justification::centred, false);
    }

    const auto fullScale = 1.0f / appliedGain;

    for (int row = 0; row <= 8; row += 2)
    {
        const auto y = plot.getY() + plot.getHeight() * (float) row / 8.0f;
        const auto amplitude = fullScale * (1.0f - 2.0f * (float) row / 8.0f);

        g.drawText (juce::String (amplitude, 2),
                    juce::Rectangle<float> (plot.getX() - (float) Layout::axisGutterLeft, y - 9.0f,
                                            (float) Layout::axisGutterLeft - 8.0f, 18.0f),
                    juce::Justification::centredRight, false);
    }

    // One status line, at the top left, rather than a note in each corner. The
    // bottom corner was the first attempt and the trace runs straight through
    // it on anything with energy below the centre line.
    const auto status = juce::String (scope.isTriggered() ? "Triggered" : "Free running")
                            + "   Peak " + juce::String (juce::Decibels::gainToDecibels (peak, -120.0f), 1) + " dB"
                            + (gainIndex == 0 ? "   Auto " + utf8 ("\xc3\x97") + juce::String (appliedGain, 1)
                                              : juce::String());

    g.setColour (scope.isTriggered() ? t.bedDim : t.warning);
    g.setFont (t.axisFont());
    g.drawText (status,
                juce::Rectangle<float> (plot.getX() + 8.0f, plot.getY() + 6.0f, 320.0f, 18.0f),
                juce::Justification::centredLeft, false);
}

void ScopePage::paintTrace (juce::Graphics& g, const std::vector<float>& samples, int count,
                            juce::Colour colour, float gain, float thickness) const
{
    if (count <= 1 || (int) samples.size() < count)
        return;

    const auto plot = plotArea();
    const auto centreY = plot.getCentreY();
    const auto halfHeight = plot.getHeight() * 0.5f;

    juce::Path trace;

    // One vertical line per pixel column when there are more samples than
    // pixels, rather than one path point per sample. A 200 ms window at 96 kHz
    // is nineteen thousand points through a path rasteriser, and the result is
    // a solid block that took a millisecond to draw.
    const auto width = (int) plot.getWidth();

    if (count > width * 2 && width > 0)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto first = (int) ((int64_t) x * count / width);
            const auto last  = juce::jmin (count - 1, (int) ((int64_t) (x + 1) * count / width) - 1);

            float low = 1.0e9f, high = -1.0e9f;

            for (int i = first; i <= last; ++i)
            {
                low  = juce::jmin (low,  samples[(size_t) i]);
                high = juce::jmax (high, samples[(size_t) i]);
            }

            const auto topY    = centreY - juce::jlimit (-1.0f, 1.0f, high * gain) * halfHeight;
            const auto bottomY = centreY - juce::jlimit (-1.0f, 1.0f, low  * gain) * halfHeight;

            g.setColour (colour);
            g.fillRect (plot.getX() + (float) x, topY, 1.0f, juce::jmax (1.0f, bottomY - topY));
        }

        return;
    }

    for (int i = 0; i < count; ++i)
    {
        const auto x = plot.getX() + plot.getWidth() * (float) i / (float) (count - 1);
        const auto y = centreY - juce::jlimit (-1.0f, 1.0f, samples[(size_t) i] * gain) * halfHeight;

        if (i == 0)
            trace.startNewSubPath (x, y);
        else
            trace.lineTo (x, y);
    }

    g.setColour (colour);
    g.strokePath (trace, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}
