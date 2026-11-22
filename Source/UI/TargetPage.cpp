//
//  TargetPage.cpp
//  FX Analyzer
//

#include "TargetPage.h"

#include "../PluginProcessor.h"
#include "GraphAxes.h"

TargetPage::TargetPage (FXAnalyzerProcessor& processor) : PageBase (processor)
{
}

void TargetPage::pageShown()
{
    // This is what starts the measurement: it runs only while this page is on screen.
    plugin.getAnalysis().setActivePage (FXParams::Page::target);
    lastRefreshMs = 0.0;
}

void TargetPage::refresh()
{
    auto& hub = plugin.getAnalysis();

    // Freeze holds the picture, as on every other page.
    if (hub.isFrozen())
        return;

    const auto& engine = hub.getTarget();
    const auto newBands = engine.getNumBands();

    // Each value glides towards the engine's, whatever the timer rate; the first
    // refresh (and a new band layout) snaps.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto snap = lastRefreshMs <= 0.0 || newBands != bands;
    const auto dt = snap ? 0.0 : juce::jlimit (0.0, 0.25, 0.001 * (now - lastRefreshMs));
    lastRefreshMs = now;
    const auto k = snap ? 0.0f : (float) std::exp (-dt / glideSeconds);

    bands = newBands;
    for (int b = 0; b < bands; ++b)
    {
        hz[(size_t) b] = engine.getBandFrequency (b);
        deviation[(size_t) b] = engine.getDisplayDeviationDb (b) + (deviation[(size_t) b] - engine.getDisplayDeviationDb (b)) * k;
        confidence[(size_t) b] = engine.getDisplayConfidence (b) + (confidence[(size_t) b] - engine.getDisplayConfidence (b)) * k;
    }

    holding = engine.isHolding();

    // The octave values: the mean deviation of the bands within half an octave,
    // weighted by confidence; a band the gate counts as missing shows a dash.
    for (int i = 0; i < numOctaves; ++i)
    {
        float sum = 0.0f, weight = 0.0f;
        int count = 0;
        for (int b = 0; b < bands; ++b)
            if (std::abs (std::log2 (hz[(size_t) b] / octaveHz[(size_t) i])) <= 0.5f)
            {
                sum += confidence[(size_t) b] * deviation[(size_t) b];
                weight += confidence[(size_t) b];
                ++count;
            }

        octaveValid[(size_t) i] = count > 0 && weight / (float) count >= 0.5f;
        octaveDb[(size_t) i] = octaveValid[(size_t) i] ? std::round (2.0f * sum / weight) / 2.0f : 0.0f;
    }

    repaint();
}

juce::Rectangle<float> TargetPage::displayArea() const
{
    return getLocalBounds().reduced (Layout::pageMarginX, Layout::pageMarginY).toFloat();
}

/** The deviation at t as Clearbox draws it: the 1-2-1 smoothed band values joined
    by Catmull-Rom, carried on below the lowest band with the slope of the two
    lowest (at most 12 dB), flat above the top band. */
float TargetPage::splineAt (float t) const noexcept
{
    const auto tFor = [] (float f) { return std::log (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f); };
    const auto v = [&] (int b)
    {
        b = juce::jlimit (0, bands - 1, b);
        return 0.25f * deviation[(size_t) juce::jmax (0, b - 1)] + 0.5f * deviation[(size_t) b]
             + 0.25f * deviation[(size_t) juce::jmin (bands - 1, b + 1)];
    };

    const auto t0 = tFor (hz[0]);
    if (t <= t0)
    {
        const auto z0 = ClearEngine::erbNumber (hz[0]), z1 = ClearEngine::erbNumber (hz[1]);
        const auto bandsBelow = (ClearEngine::erbNumber (20.0f) - z0) / (z1 - z0);
        const auto at20 = v (0) + juce::jlimit (-12.0f, 12.0f, bandsBelow * (v (1) - v (0)));
        return t0 <= 0.0f ? v (0) : at20 + (v (0) - at20) * t / t0;
    }

    for (int b = 0; b + 1 < bands; ++b)
    {
        const auto ta = tFor (hz[(size_t) b]), tb = tFor (hz[(size_t) (b + 1)]);
        if (t <= tb)
        {
            const auto u = (t - ta) / juce::jmax (1.0e-6f, tb - ta), u2 = u * u, u3 = u2 * u;
            const auto p0 = v (b - 1), p1 = v (b), p2 = v (b + 1), p3 = v (b + 2);
            return 0.5f * (2.0f * p1 + (p2 - p0) * u + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2
                           + (3.0f * p1 - p0 - 3.0f * p2 + p3) * u3);
        }
    }

    return v (bands - 1);
}

void TargetPage::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto display = displayArea();
    const auto plot = GraphAxes::plotInDisplay (display);

    g.setColour (t.background);
    g.fillRect (getLocalBounds());

    GraphAxes::paintDisplay (g, t, display);
    GraphAxes::paintBed (g, t, plot);

    const auto dbStrip = juce::Rectangle<float> (plot.getX() - (float) Layout::axisGutterLeft, plot.getY(),
                                                 (float) Layout::axisGutterLeft - 8.0f, plot.getHeight());
    const auto freqStrip = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 2.0f,
                                                   plot.getWidth(), (float) Layout::axisGutterBottom - 6.0f);

    GraphAxes::paintDecibelGrid (g, t, plot, dbStrip, rangeDb, -rangeDb, 5.0f);
    GraphAxes::paintFrequencyGrid (g, t, plot, freqStrip, 20.0f, 20000.0f, true);

    const auto yFor = [&] (float db) { return GraphAxes::decibelsToY (juce::jlimit (-rangeDb, rangeDb, db), plot, rangeDb, -rangeDb); };

    if (bands >= 2)
    {
        juce::Path curve;
        for (int i = 0; i < gridPoints; ++i)
        {
            const auto u = (float) i / (float) (gridPoints - 1);
            const auto x = plot.getX() + plot.getWidth() * u;
            if (i == 0) curve.startNewSubPath (x, yFor (splineAt (u))); else curve.lineTo (x, yFor (splineAt (u)));
        }

        juce::Path filled (curve);
        filled.lineTo (plot.getRight(), yFor (0.0f));
        filled.lineTo (plot.getX(), yFor (0.0f));
        filled.closeSubPath();

        const auto zero = yFor (0.0f);
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (plot.withBottom (zero).toNearestInt());
            g.setColour (t.curveFill());
            g.fillPath (filled);
        }
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (plot.withTop (zero).toNearestInt());
            g.setColour (t.below.withMultipliedAlpha (Theme::curveFillAlpha));
            g.fillPath (filled);
        }

        g.setColour (t.curve);
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // One value per octave, just above the frequency labels.
    g.setFont (Theme::numberFont (10.0f, true));
    for (int i = 0; i < numOctaves; ++i)
    {
        const auto x = GraphAxes::frequencyToX (octaveHz[(size_t) i], plot, 20.0f, 20000.0f, true);
        const auto box = juce::Rectangle<float> (x - 22.0f, plot.getBottom() - 18.0f, 44.0f, 14.0f);
        g.setColour (t.bed.withAlpha (0.85f));
        g.fillRoundedRectangle (box, 3.0f);

        if (octaveValid[(size_t) i])
        {
            const auto v = octaveDb[(size_t) i];
            g.setColour (v > 0.0f ? t.bedText : (v < 0.0f ? t.below : t.bedDim));
            g.drawText ((v > 0.0f ? "+" : "") + juce::String (v, 1), box, juce::Justification::centred, false);
        }
        else
        {
            g.setColour (t.bedDim);
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")), box, juce::Justification::centred, false);
        }
    }

    // The header, on its own backing: a large deviation runs right up into it.
    const auto font = t.axisFont();
    g.setFont (font);
    const auto header = plot.reduced (6.0f, 4.0f).removeFromTop (12.0f);

    const auto label = [&] (const juce::String& text, bool right, juce::Colour colour)
    {
        const auto w = juce::GlyphArrangement::getStringWidth (font, text) + 10.0f;
        const auto box = right ? header.withLeft (header.getRight() - w) : header.withWidth (w);
        g.setColour (t.bed.withAlpha (0.85f));
        g.fillRoundedRectangle (box.expanded (0.0f, 2.0f).translated (right ? 5.0f : -5.0f, 0.0f), 3.0f);
        g.setColour (colour);
        g.drawText (text, header, right ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
    };

    if (holding)
        label ("HOLD", true, t.bedText);
    else
    {
        float largest = 0.0f;
        for (int i = 0; i < numOctaves; ++i)
            if (octaveValid[(size_t) i])
                largest = juce::jmax (largest, std::abs (octaveDb[(size_t) i]));
        label (juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (largest, 1) + " dB", true, t.bedText);
    }

    label (juce::String (juce::CharPointer_UTF8 ("DEVIATION FROM 4.5 DB/OCT \xc2\xb7 CLEARBOX TARGET")), false, t.bedDim);
}
