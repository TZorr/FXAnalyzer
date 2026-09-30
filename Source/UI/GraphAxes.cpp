//
//  GraphAxes.cpp
//  FX Analyzer
//

#include "GraphAxes.h"

#include <cmath>
#include <utility>
#include <vector>

namespace GraphAxes
{

//==============================================================================
// The mapping itself lives in FXParams, because AnalyzerCheck asks the same
// question - how much of the axis does a hertz occupy - without a rectangle to
// ask it about. These two add the rectangle and nothing else.
float frequencyToX (float hz, juce::Rectangle<float> plot, float minHz, float maxHz, bool logarithmic)
{
    if (plot.getWidth() <= 0.0f || maxHz <= minHz)
        return plot.getX();

    const auto proportion = FXParams::axisProportion (hz, minHz, maxHz, logarithmic);

    return plot.getX() + juce::jlimit (0.0f, 1.0f, proportion) * plot.getWidth();
}

float xToFrequency (float x, juce::Rectangle<float> plot, float minHz, float maxHz, bool logarithmic)
{
    if (plot.getWidth() <= 0.0f)
        return minHz;

    const auto proportion = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());

    return FXParams::axisFrequency (proportion, minHz, maxHz, logarithmic);
}

float decibelsToY (float db, juce::Rectangle<float> plot, float topDb, float bottomDb)
{
    if (topDb <= bottomDb)
        return plot.getBottom();

    const auto proportion = juce::jlimit (0.0f, 1.0f, (topDb - db) / (topDb - bottomDb));
    return plot.getY() + proportion * plot.getHeight();
}

float yToDecibels (float y, juce::Rectangle<float> plot, float topDb, float bottomDb)
{
    if (plot.getHeight() <= 0.0f)
        return bottomDb;

    const auto proportion = juce::jlimit (0.0f, 1.0f, (y - plot.getY()) / plot.getHeight());
    return topDb - proportion * (topDb - bottomDb);
}

//==============================================================================
void paintDisplay (juce::Graphics& g, const Theme& theme, juce::Rectangle<float> area)
{
    g.setColour (theme.bed);
    g.fillRoundedRectangle (area, 6.0f);
}

juce::Rectangle<float> plotInDisplay (juce::Rectangle<float> display)
{
    return display.withTrimmedLeft ((float) Layout::axisGutterLeft)
                  .withTrimmedBottom ((float) Layout::axisGutterBottom)
                  .withTrimmedTop (10.0f)
                  .withTrimmedRight (14.0f);
}

void paintBed (juce::Graphics& g, const Theme& theme, juce::Rectangle<float> plot)
{
    g.setColour (theme.bed);
    g.fillRect (plot);
}

//==============================================================================
juce::String formatFrequency (float hz)
{
    if (hz >= 10000.0f)
        return juce::String (juce::roundToInt (hz / 1000.0f)) + " kHz";

    if (hz >= 1000.0f)
    {
        const auto thousands = hz / 1000.0f;
        const auto rounded   = std::round (thousands * 10.0f) / 10.0f;

        return (std::abs (rounded - std::round (rounded)) < 0.05f
                    ? juce::String (juce::roundToInt (rounded))
                    : juce::String (rounded, 1)) + " kHz";
    }

    if (hz >= 100.0f)
        return juce::String (juce::roundToInt (hz)) + " Hz";

    return juce::String (hz, hz < 10.0f ? 1 : 0) + " Hz";
}

void paintFrequencyGrid (juce::Graphics& g, const Theme& theme,
                         juce::Rectangle<float> plot, juce::Rectangle<float> labelStrip,
                         float minHz, float maxHz, bool logarithmic)
{
    g.setFont (theme.axisFont());

    // The tick set has to come from the mapping, not from the frequency range.
    // The two scales need different sets and the first draft used the
    // logarithmic one for both: on a linear axis everything below 2 kHz lands
    // inside the leftmost tenth of the plot, so 20, 50, 100, 200, 500 and 1000
    // are drawn on top of each other and the result is an illegible smear
    // against the left edge. It is obvious in a screenshot and invisible in the
    // code, which is what EditorShot is for - except that it renders the
    // default, and the default is Log.
    std::vector<std::pair<float, bool>> ticks;   // frequency, and whether it is labelled

    if (logarithmic)
    {
        // The 1-2-5 sequence per decade. Every one gets a line; only the ones
        // in the labelled set get a number, because three labels per decade
        // would be thirty numbers in the width of a hand.
        static constexpr float decadeSteps[] { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f };
        static constexpr float labelled[]    { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                                               1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };

        // On the full 20 Hz - 20 kHz axis the labelled set is a decimation:
        // three numbers per decade is already thirty across the plot. On a
        // narrow span - the bass zoom's five octaves - that same set leaves
        // four numbers on seven hundred pixels, and an axis with four labels is
        // an axis you have to count along. So below six octaves every tick is
        // offered a label and the collision guard further down decides how many
        // actually fit. The guard is the same one that keeps a shrunken window
        // legible, so this needs no second rule about widths.
        const auto denseLabels = std::log2 (maxHz / juce::jmax (1.0e-3f, minHz)) <= 6.0f;

        const auto isLabelled = [denseLabels] (float hz)
        {
            if (denseLabels)
                return true;

            for (auto candidate : labelled)
                if (std::abs (hz - candidate) < candidate * 0.001f)
                    return true;

            return false;
        };

        for (float decade = 10.0f; decade <= 20000.0f; decade *= 10.0f)
            for (auto step : decadeSteps)
            {
                const auto hz = decade * step;

                if (hz >= minHz && hz <= maxHz)
                    ticks.emplace_back (hz, isLabelled (hz));
            }
    }
    else
    {
        // Evenly spaced, at a step rounded up to something a person would
        // choose. Ten labels across the plot is the target; the 1-2-5 rounding
        // is what turns a computed 1998 Hz into the 2 kHz nobody has to think
        // about.
        const auto rough = (maxHz - minHz) / 10.0f;
        const auto power = std::pow (10.0f, std::floor (std::log10 (juce::jmax (1.0f, rough))));
        const auto mantissa = rough / power;

        const auto step = power * (mantissa <= 1.0f ? 1.0f
                                                    : mantissa <= 2.0f ? 2.0f
                                                                       : mantissa <= 5.0f ? 5.0f : 10.0f);

        for (float hz = std::ceil (minHz / step) * step; hz <= maxHz; hz += step)
        {
            ticks.emplace_back (hz, true);

            if (hz + step * 0.5f <= maxHz)
                ticks.emplace_back (hz + step * 0.5f, false);
        }
    }

    // Labels are laid down left to right and one is skipped whenever it would
    // touch the one before it. That guard is the part that makes this safe at
    // any window width: a panel dragged down to three quarters of its size has
    // three quarters of the room for the same number of labels.
    float lastLabelRight = -1.0e9f;

    for (const auto& [hz, labelled] : ticks)
    {
        const auto x = frequencyToX (hz, plot, minHz, maxHz, logarithmic);

        g.setColour (labelled ? theme.grid : theme.gridMinor());
        g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());

        if (! labelled || labelStrip.isEmpty())
            continue;

        const auto text  = formatFrequency (hz);
        const auto width = juce::GlyphArrangement::getStringWidth (theme.axisFont(), text) + 8.0f;

        // Centred on the line, then pulled back inside the strip at the two
        // ends so "20 Hz" and "20 kHz" are not half cut off by the window edge.
        auto area = juce::Rectangle<float> (width, labelStrip.getHeight())
                        .withCentre ({ x, labelStrip.getCentreY() });

        area.setX (juce::jlimit (labelStrip.getX(), labelStrip.getRight() - width, area.getX()));

        if (area.getX() < lastLabelRight)
            continue;

        lastLabelRight = area.getRight();

        g.setColour (theme.bedDim);
        g.drawText (text, area, juce::Justification::centred, false);
    }
}

void paintDecibelGrid (juce::Graphics& g, const Theme& theme,
                       juce::Rectangle<float> plot, juce::Rectangle<float> labelStrip,
                       float topDb, float bottomDb, float stepDb)
{
    if (stepDb <= 0.0f)
        return;

    g.setFont (theme.axisFont());

    for (float db = topDb; db >= bottomDb - 0.001f; db -= stepDb)
    {
        const auto y = decibelsToY (db, plot, topDb, bottomDb);

        // 0 dB is the reference every other number on the panel is quoted
        // against, so it is drawn as an axis rather than as one more gridline.
        const auto isZero = std::abs (db) < 0.001f;

        g.setColour (isZero ? theme.grid.brighter (0.25f) : theme.gridMinor());
        g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);

        if (! labelStrip.isEmpty())
        {
            g.setColour (theme.bedDim);

            const auto text = (db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db)) + " dB";

            auto area = juce::Rectangle<float> (labelStrip.getWidth(), theme.axisSize + 6.0f)
                            .withCentre ({ labelStrip.getCentreX(), y });

            area.setY (juce::jlimit (labelStrip.getY(), labelStrip.getBottom() - area.getHeight(), area.getY()));

            g.drawText (text, area, juce::Justification::centredRight, false);
        }
    }
}

void paintDivisionGrid (juce::Graphics& g, const Theme& theme,
                        juce::Rectangle<float> plot, int columns, int rows)
{
    g.setColour (theme.gridMinor());

    for (int i = 1; i < columns; ++i)
        g.fillRect (plot.getX() + plot.getWidth() * (float) i / (float) columns,
                    plot.getY(), 1.0f, plot.getHeight());

    for (int i = 1; i < rows; ++i)
    {
        const auto y = plot.getY() + plot.getHeight() * (float) i / (float) rows;
        const auto middle = rows % 2 == 0 && i == rows / 2;

        g.setColour (middle ? theme.grid.brighter (0.25f) : theme.gridMinor());
        g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
    }
}

} // namespace GraphAxes
