//
//  SpectrumPage.cpp
//  FX Analyzer
//

#include "SpectrumPage.h"
#include "../PluginProcessor.h"

#include <cmath>

//==============================================================================
SpectrumPage::SpectrumPage (FXAnalyzerProcessor& processor) : PageBase (processor)
{
    // No steppers here any more. Since 2026-09-24 every setting lives on the
    // Settings page, over a live preview of this graph, and this page is the
    // graph alone - it gets the full width of the panel instead of giving 92
    // points of it to a column. The three buttons stay: cursor, bass zoom and
    // freeze are things done while looking, not settings.
    addAndMakeVisible (cursorButton);
    addAndMakeVisible (bassButton);
    addAndMakeVisible (freezeButton);

    cursorButton.setTooltip ("Read frequency and level under the pointer."
                            " The level is time-smoothed, so Reactivity decides how still it stands");
    bassButton.setTooltip ("Zoom the axis to the bass band, up to 320 Hz."
                           " This magnifies the measurement; it does not refine it -"
                           " for that, raise FFT Size");
    freezeButton.setTooltip ("Hold the display");

    cursorButton.onClick = [this] (bool) { repaint(); };

    bassButton.onClick = [this] (bool state)
    {
        if (syncing)
            return;

        plugin.setViewProperty (FXParams::propSpectrumBassZoom, state ? 1 : 0);
        syncFromState();
        repaint();
    };

    freezeButton.onClick = [this] (bool state)
    {
        if (! syncing)
            plugin.setBoolParameter (FXParams::freeze, state);
    };

    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

//==============================================================================
void SpectrumPage::themeChanged()
{


    sonogramValid = false;
}

void SpectrumPage::pageShown()
{
    syncFromState();

    // A preview is not the page being looked at; the Settings page that holds
    // it says which page is active.
    if (! embedded)
        plugin.getAnalysis().setActivePage (FXParams::Page::spectrum);
}

void SpectrumPage::setEmbedded (bool shouldBeEmbedded)
{
    embedded = shouldBeEmbedded;

    for (auto* button : { &cursorButton, &bassButton, &freezeButton })
        button->setVisible (! embedded);

    // A preview is looked at, not pointed into: no crosshair, and no clicks
    // taken from the Settings page around it.
    setMouseCursor (embedded ? juce::MouseCursor::NormalCursor : juce::MouseCursor::CrosshairCursor);
    setInterceptsMouseClicks (! embedded, ! embedded);

    resized();
}

juce::String SpectrumPage::formatTilt (float dbPerOctave)
{
    // One decimal only where there is one. "4.5 dB/oct" and "3 dB/oct" are
    // what the old list said; "3.0 dB/oct" beside them would read as a
    // different setting.
    const auto rounded = std::round (dbPerOctave * 10.0f) / 10.0f;

    return (std::abs (rounded - std::round (rounded)) < 0.05f
                ? juce::String (juce::roundToInt (rounded))
                : juce::String (rounded, 1))
           + " dB/oct";
}

void SpectrumPage::syncFromState()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    const auto modeIndex  = (int) plugin.getViewProperty (FXParams::propSpectrumMode, 0);
    const auto scaleIndex = (int) plugin.getViewProperty (FXParams::propSpectrumScale, 0);

    mode  = (FXParams::SpectrumMode)   juce::jlimit (0, FXParams::lastSpectrumMode, modeIndex);
    scale = (FXParams::FrequencyScale) juce::jlimit (0, 1, scaleIndex);
    slopeDbPerOctave = plugin.getSpectrumTiltDb();
    tiltPivotHz      = plugin.getSpectrumTiltPivotHz();

    const auto topIndex = juce::jlimit (0, FXParams::spectrumTopNames.size() - 1,
                                        (int) plugin.getViewProperty (FXParams::propSpectrumTop,
                                                                      FXParams::defaultSpectrumTopIndex));

    const auto rangeIndex = juce::jlimit (0, FXParams::spectrumRangeNames.size() - 1,
                                          (int) plugin.getViewProperty (FXParams::propSpectrumRange,
                                                                        FXParams::defaultSpectrumRangeIndex));

    topDb    = FXParams::spectrumTopValues[topIndex];
    bottomDb = topDb - FXParams::spectrumRangeValues[rangeIndex];

    bassZoom = (int) plugin.getViewProperty (FXParams::propSpectrumBassZoom, 0) != 0;

    const auto logarithmic = scale == FXParams::FrequencyScale::logarithmic;

    // 0 Hz on a logarithmic axis is not a small number, it is no number at all,
    // so the zoom starts an octave below the normal axis there and takes the
    // literal zero only on the linear one. Nothing musical is given up either
    // way: the DC blocker has already removed everything under 5 Hz.
    viewMinHz = bassZoom ? (logarithmic ? FXParams::bassZoomMinHzLog : FXParams::bassZoomMinHzLinear)
                         : FXParams::spectrumMinHz;

    viewMaxHz = bassZoom ? FXParams::bassZoomMaxHz : FXParams::spectrumMaxHz;

    freezeButton.setToggleState (plugin.getBoolParameter (FXParams::freeze));
    bassButton.setToggleState (bassZoom);

    // The sonogram's colours are a mapping from this range, so a change of
    // range makes every row already drawn wrong. Discarding it is the honest
    // answer: the alternative is a picture whose top half and bottom half are
    // scaled differently and nothing on screen says so.
    sonogramValid = false;
}

//==============================================================================
juce::Rectangle<float> SpectrumPage::plotArea() const
{
    auto area = getLocalBounds().reduced (10, 10);
    area.removeFromLeft (Layout::axisGutterLeft);
    area.removeFromBottom (Layout::axisGutterBottom);

    return area.toFloat();
}

void SpectrumPage::resized()
{
    const auto plot = plotArea().toNearestInt();

    freezeButton.setBounds (plot.getRight() - 46,  plot.getY() + 8, 38, 38);
    bassButton.setBounds   (plot.getRight() - 94,  plot.getY() + 8, 38, 38);
    cursorButton.setBounds (plot.getRight() - 142, plot.getY() + 8, 38, 38);

    sonogramValid = false;
}

//==============================================================================
void SpectrumPage::refresh()
{
    rebuildColumns();

    if (mode == FXParams::SpectrumMode::sonogram && ! plugin.getAnalysis().isFrozen())
        appendSonogramRow();

    freezeButton.setToggleState (plugin.getBoolParameter (FXParams::freeze));

    repaint();
}

SpectrumPage::Reading SpectrumPage::sampleTier (int tierIndex, float lowHz, float highHz,
                                                Source source) const
{
    const auto& analyser = plugin.getAnalysis().getTierAnalyser (tierIndex);

    const auto drawn = source == Source::drawn;

    const auto& magnitudes = drawn ? analyser.getDisplayDb()     : analyser.getMagnitudesDb();
    const auto& peaks      = drawn ? analyser.getDisplayPeakDb() : analyser.getPeakDb();

    Reading reading;

    const auto bins = (int) magnitudes.size();

    if (bins < 2)
        return reading;

    const auto scaleToBin = (double) analyser.getFftSize() / analyser.getSampleRate();
    const auto lowBin  = (double) lowHz  * scaleToBin;
    const auto highBin = (double) highHz * scaleToBin;

    const auto firstBin = (int) std::ceil (lowBin);
    const auto lastBin  = (int) std::floor (highBin);

    if (firstBin <= lastBin)
    {
        // More than one bin under this pixel: take the loudest. A mean would
        // hide the narrow peak this display exists to find. It also loses the
        // others, which is the entire case for the bass zoom - see
        // FXParams::binsPerPixel.
        reading.levelDb = reading.peakDb = SpectrumAnalyser::floorDb;

        for (int bin = juce::jmax (0, firstBin); bin <= juce::jmin (bins - 1, lastBin); ++bin)
        {
            reading.levelDb = juce::jmax (reading.levelDb, magnitudes[(size_t) bin]);
            reading.peakDb  = juce::jmax (reading.peakDb,  peaks[(size_t) bin]);
        }
    }
    else
    {
        // Fewer than one bin per pixel. Interpolating rather than repeating the
        // nearest bin is what stops the low end drawing as a staircase.
        const auto position = (lowBin + highBin) * 0.5;
        const auto lower    = juce::jlimit (0, bins - 1, (int) std::floor (position));
        const auto upper    = juce::jlimit (0, bins - 1, lower + 1);
        const auto fraction = (float) (position - std::floor (position));

        reading.levelDb = magnitudes[(size_t) lower] + (magnitudes[(size_t) upper] - magnitudes[(size_t) lower]) * fraction;
        reading.peakDb  = peaks[(size_t) lower]      + (peaks[(size_t) upper]      - peaks[(size_t) lower])      * fraction;
    }

    return reading;
}

SpectrumPage::Reading SpectrumPage::readTiers (float lowHz, float highHz, Source source) const
{
    const auto& analysis = plugin.getAnalysis();
    const auto  choice   = analysis.chooseTier (0.5f * (lowHz + highHz));

    auto reading = sampleTier (choice.primary, lowHz, highHz, source);

    if (choice.secondaryWeight > 0.0f)
    {
        const auto other = sampleTier (choice.secondary, lowHz, highHz, source);

        reading.levelDb = reading.levelDb * (1.0f - choice.secondaryWeight) + other.levelDb * choice.secondaryWeight;
        reading.peakDb  = reading.peakDb  * (1.0f - choice.secondaryWeight) + other.peakDb  * choice.secondaryWeight;
    }

    return reading;
}

void SpectrumPage::rebuildColumns()
{
    const auto plot  = plotArea();
    const auto width = juce::jmax (0, (int) plot.getWidth());

    if (width == 0)
        return;

    columnDb.assign     ((size_t) width, SpectrumAnalyser::floorDb);
    columnPeakDb.assign ((size_t) width, SpectrumAnalyser::floorDb);

    const auto logarithmic = scale == FXParams::FrequencyScale::logarithmic;

    if (plugin.getAnalysis().getSpectrum().getDisplayDb().empty())
        return;

    for (int x = 0; x < width; ++x)
    {
        const auto lowHz  = GraphAxes::xToFrequency (plot.getX() + (float) x,
                                                     plot, viewMinHz, viewMaxHz, logarithmic);
        const auto highHz = GraphAxes::xToFrequency (plot.getX() + (float) x + 1.0f,
                                                     plot, viewMinHz, viewMaxHz, logarithmic);

        auto reading = readTiers (lowHz, highHz, Source::drawn);

        if (slopeDbPerOctave != 0.0f)
        {
            const auto centreHz = 0.5f * (lowHz + highHz);
            const auto tilt = FXParams::spectrumTiltAt (centreHz, slopeDbPerOctave, tiltPivotHz);

            reading.levelDb += tilt;
            reading.peakDb  += tilt;
        }

        columnDb[(size_t) x]     = reading.levelDb;
        columnPeakDb[(size_t) x] = reading.peakDb;
    }
}

//==============================================================================
void SpectrumPage::appendSonogramRow()
{
    const auto plot   = plotArea().toNearestInt();
    const auto width  = juce::jmax (1, plot.getWidth());
    const auto height = juce::jmax (1, plot.getHeight());

    if (! sonogramValid || sonogram.getWidth() != width || sonogram.getHeight() != height)
    {
        sonogram = juce::Image (juce::Image::ARGB, width, height, true);
        sonogramRow = 0;
        sonogramValid = true;
    }

    if (columnDb.size() < (size_t) width)
        return;

    juce::Image::BitmapData pixels (sonogram, juce::Image::BitmapData::writeOnly);

    const auto& t = theme();

    for (int x = 0; x < width; ++x)
    {
        const auto db = columnDb[(size_t) x];
        const auto proportion = juce::jlimit (0.0f, 1.0f,
                                              (db - bottomDb) / (topDb - bottomDb));

        // Two-stop ramp through the theme's own colours: quiet is the graph
        // bed, loud is the curve colour, and the top of the range runs on into
        // the text colour so that a peak is distinguishable from merely loud.
        // A rainbow map would be prettier and would encode level in hue, which
        // nobody can read back to a number.
        const auto colour = proportion < 0.75f
            ? t.background.interpolatedWith (t.curve, proportion / 0.75f)
            : t.curve.interpolatedWith (t.text, (proportion - 0.75f) / 0.25f);

        pixels.setPixelColour (x, sonogramRow, colour);
    }

    sonogramRow = (sonogramRow + 1) % height;
}

//==============================================================================
void SpectrumPage::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto plot = plotArea();

    g.setColour (t.background);
    g.fillRect (getLocalBounds());

    GraphAxes::paintBed (g, t, plot);

    auto dbStrip = juce::Rectangle<float> (plot.getX() - (float) Layout::axisGutterLeft,
                                           plot.getY(),
                                           (float) Layout::axisGutterLeft - 6.0f,
                                           plot.getHeight());

    auto freqStrip = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 2.0f,
                                             plot.getWidth(), (float) Layout::axisGutterBottom - 4.0f);

    // The step the range asks for, doubled until the labels have room. Only a
    // short graph ever doubles it - the Settings preview, a small window - and
    // there the labels used to stand on each other: the top one is clamped
    // into the strip, so at 16 points per step "-6 dB" and "-16 dB" overlapped.
    auto gridStepDb = FXParams::spectrumGridStepDb (topDb - bottomDb);

    while (plot.getHeight() * gridStepDb / (topDb - bottomDb) < t.axisSize * 1.8f
           && gridStepDb < topDb - bottomDb)
        gridStepDb *= 2.0f;

    GraphAxes::paintDecibelGrid (g, t, plot, dbStrip, topDb, bottomDb, gridStepDb);

    GraphAxes::paintFrequencyGrid (g, t, plot, freqStrip,
                                   viewMinHz, viewMaxHz,
                                   scale == FXParams::FrequencyScale::logarithmic);

    {
        const juce::Graphics::ScopedSaveState clipToPlot (g);
        g.reduceClipRegion (plot.toNearestInt());

        switch (mode)
        {
            case FXParams::SpectrumMode::bars:
            case FXParams::SpectrumMode::bars63:   paintBars (g, FXParams::spectrumBarCount (mode)); break;
            case FXParams::SpectrumMode::sonogram: paintSonogram (g); break;
            case FXParams::SpectrumMode::curve:
            default:                               paintCurve (g);    break;
        }
    }

    // The measurement's own terms, on the page that shows it. Somebody reading
    // a peak at 43 Hz needs to know the bins are 11.7 Hz apart before they
    // believe it to the digit.
    const auto& analysis = plugin.getAnalysis();
    const auto& finest   = analysis.getTierAnalyser (analysis.getNumTiers() - 1);
    const auto resolution = finest.getSampleRate() / (double) finest.getFftSize();

    auto info = analysis.describeTiers() + "  "
                    + juce::String (resolution, 2) + " Hz/bin at the bottom  "
                    + juce::String (finest.getSampleRate() / 1000.0, 1) + " kHz";

    // With the zoom on, say what the band actually contains. The question the
    // button raises - does looking at less of the spectrum show more of it - is
    // answered by two numbers, and they belong on the page rather than in a
    // document nobody has open while they are using it. The count is of real
    // bins between the axis edges; the window length is what fixes the spacing,
    // and it is the only one of the three a setting can change.
    if (bassZoom)
    {
        const auto binWidthHz = (float) (finest.getSampleRate() / (double) finest.getFftSize());
        const auto binsInBand = (int) std::floor ((viewMaxHz - juce::jmax (0.0f, viewMinHz)) / binWidthHz);

        // juce::String (float, 0) does not mean "no decimals" - it means "as
        // many as it takes", which is how 1365.33 ms ended up on the panel.
        const auto windowMs = 1000.0f * (float) finest.getFftSize() / (float) finest.getSampleRate();

        const auto windowText = windowMs >= 1000.0f
            ? juce::String (windowMs / 1000.0f, 2) + " s window"
            : juce::String (juce::roundToInt (windowMs)) + " ms window";

        info += "   " + GraphAxes::formatFrequency (juce::jmax (0.0f, viewMinHz))
                      + juce::String ("-") + GraphAxes::formatFrequency (viewMaxHz)
                      + "  " + juce::String (binsInBand) + " bins  " + windowText;
    }

    // When Multi is set but cannot be honoured, say so here rather than leave
    // the Settings page claiming one thing and the display doing another. This
    // is the whole of the "Resolution sometimes doesn't switch" report: it did
    // switch, smoothing was off, and nothing on the panel connected the two.
    const auto wantsMulti = (int) plugin.getViewProperty (FXParams::propSpectrumResolution,
                                                          FXParams::defaultSpectrumResolutionIndex)
                                == (int) FXParams::SpectrumResolution::multi;

    const auto smoothingIndex = juce::jlimit (0, FXParams::spectrumSmoothingNames.size() - 1,
                                              (int) plugin.getViewProperty (FXParams::propSpectrumSmoothing,
                                                                            FXParams::defaultSpectrumSmoothingIndex));

    const auto suppressed = wantsMulti && ! FXParams::multiResolutionAvailable (smoothingIndex);

    g.setColour (suppressed ? t.warning : t.dimText());
    g.setFont (t.axisFont());
    // Up to the leftmost button and no further. The width used to be a flat
    // 460, which was comfortable until the line grew a bass band on the end.
    const auto infoWidth = embedded ? plot.getWidth() - 16.0f
                                    : juce::jmax (200.0f, (float) cursorButton.getX() - plot.getX() - 16.0f);

    g.drawText (suppressed ? info + "   Multi needs Bands (not Off)" : info,
                juce::Rectangle<float> (plot.getX() + 8.0f, plot.getY() + 6.0f, infoWidth, 18.0f),
                juce::Justification::centredLeft, false);

    if (cursorButton.getToggleState() && cursorInside && ! embedded)
        paintCursorReadout (g);
}

void SpectrumPage::paintCurve (juce::Graphics& g) const
{
    const auto plot = plotArea();

    if (columnDb.empty())
        return;

    const auto& t = theme();

    juce::Path curve, fill;

    for (size_t x = 0; x < columnDb.size(); ++x)
    {
        const auto px = plot.getX() + (float) x;
        const auto py = GraphAxes::decibelsToY (columnDb[x], plot,
                                                topDb, bottomDb);

        if (x == 0)
        {
            curve.startNewSubPath (px, py);
            fill.startNewSubPath (px, plot.getBottom());
            fill.lineTo (px, py);
        }
        else
        {
            curve.lineTo (px, py);
            fill.lineTo (px, py);
        }
    }

    fill.lineTo (plot.getX() + (float) (columnDb.size() - 1), plot.getBottom());
    fill.closeSubPath();

    g.setColour (t.curveFill());
    g.fillPath (fill);

    g.setColour (t.curve);
    g.strokePath (curve, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Peak hold, drawn as a hairline in the second colour. It is deliberately
    // thinner than the live curve: it is a reference mark, not a second signal.
    juce::Path peak;

    for (size_t x = 0; x < columnPeakDb.size(); ++x)
    {
        const auto px = plot.getX() + (float) x;
        const auto py = GraphAxes::decibelsToY (columnPeakDb[x], plot,
                                                topDb, bottomDb);

        if (x == 0)
            peak.startNewSubPath (px, py);
        else
            peak.lineTo (px, py);
    }

    g.setColour (t.curveAlt.withAlpha (0.75f));
    g.strokePath (peak, juce::PathStrokeType (1.0f));
}

void SpectrumPage::paintBars (juce::Graphics& g, int barCount) const
{
    const auto plot = plotArea();

    if (columnDb.empty())
        return;

    const auto& t = theme();

    // 31 or 63 bars - see FXParams::spectrumBarCount. Equal widths across the
    // plot, so on the log axis each is the same fraction of an octave. At 63 on
    // the default panel a bar is still about twelve points wide.
    if (barCount <= 0)
        return;

    const auto barWidth = plot.getWidth() / (float) barCount;

    for (int bar = 0; bar < barCount; ++bar)
    {
        const auto firstColumn = (int) ((float) bar * barWidth);
        const auto lastColumn  = juce::jmin ((int) columnDb.size() - 1, (int) ((float) (bar + 1) * barWidth) - 1);

        float loudest = SpectrumAnalyser::floorDb;
        float loudestPeak = SpectrumAnalyser::floorDb;

        for (int column = firstColumn; column <= lastColumn; ++column)
        {
            loudest     = juce::jmax (loudest,     columnDb[(size_t) column]);
            loudestPeak = juce::jmax (loudestPeak, columnPeakDb[(size_t) column]);
        }

        const auto top = GraphAxes::decibelsToY (loudest, plot,
                                                 topDb, bottomDb);

        const juce::Rectangle<float> body (plot.getX() + (float) bar * barWidth + 1.0f,
                                           top,
                                           barWidth - 2.0f,
                                           plot.getBottom() - top);

        g.setColour (t.curve.withAlpha (0.85f));
        g.fillRect (body);

        const auto peakY = GraphAxes::decibelsToY (loudestPeak, plot,
                                                   topDb, bottomDb);

        g.setColour (t.curveAlt);
        g.fillRect (body.getX(), peakY - 1.0f, body.getWidth(), 2.0f);
    }
}

void SpectrumPage::paintSonogram (juce::Graphics& g) const
{
    const auto plot = plotArea().toNearestInt();

    if (! sonogramValid || sonogram.isNull())
        return;

    const auto height = sonogram.getHeight();

    // The ring unrolled into two contiguous blits. The oldest row is the one
    // about to be overwritten, so it is the row at sonogramRow, and time runs
    // downwards from there to the newest row just above it.
    const auto topHeight    = height - sonogramRow;
    const auto bottomHeight = sonogramRow;

    if (topHeight > 0)
        g.drawImage (sonogram,
                     plot.getX(), plot.getY(), plot.getWidth(), topHeight,
                     0, sonogramRow, sonogram.getWidth(), topHeight);

    if (bottomHeight > 0)
        g.drawImage (sonogram,
                     plot.getX(), plot.getY() + topHeight, plot.getWidth(), bottomHeight,
                     0, 0, sonogram.getWidth(), bottomHeight);
}

void SpectrumPage::paintCursorReadout (juce::Graphics& g) const
{
    const auto plot = plotArea();

    if (! plot.contains (cursorPosition.toFloat()))
        return;

    const auto& t = theme();
    const auto logarithmic = scale == FXParams::FrequencyScale::logarithmic;

    const auto hz     = GraphAxes::xToFrequency ((float) cursorPosition.x, plot,
                                                 viewMinHz, viewMaxHz, logarithmic);
    const auto nextHz = GraphAxes::xToFrequency ((float) cursorPosition.x + 1.0f, plot,
                                                 viewMinHz, viewMaxHz, logarithmic);

    // The measurement, not the picture of it.
    //
    // The obvious source is columnDb, which is what the curve is drawn from -
    // and it is wrong for this, twice over. It carries the slope tilt, which
    // exists only to make the curve easier to compare by eye, and it carries
    // the octave smoothing, which has averaged the bin together with its
    // neighbours. Reading it would make the cursor report a number that appears
    // nowhere in the audio and changes when you alter a display setting. The
    // readout goes back to the unsmoothed, untilted spectrum instead, which is
    // the number somebody is about to type into an equaliser.
    //
    // Unsmoothed across frequency, that is. Across time it is smoothed, and
    // deliberately: the instantaneous spectrum that used to be read here is one
    // frame of one transform, and on music it moved far too fast to read. What
    // is on screen now is the same time constant the curve is drawn with, so
    // Reactivity decides how still the number stands.
    //
    // The span is the pixel under the pointer rather than the nearest bin, so
    // that the number belongs to the place on screen the crosshair is drawn
    // through, at any zoom.
    const auto db = readTiers (hz, nextHz, Source::measured).levelDb;

    g.setColour (t.text.withAlpha (0.45f));
    g.fillRect ((float) cursorPosition.x, plot.getY(), 1.0f, plot.getHeight());

    // The note name matters more than it looks. A resonance at 98 Hz is a G,
    // and knowing that is the difference between notching a frequency and
    // understanding that the room is ringing on the root of the song.
    const auto note = NoteName::fromFrequency (hz, plugin.getAnalysis().getPitchReferenceHz());

    const auto text = GraphAxes::formatFrequency (hz)
                          + "   " + juce::String (db, 1) + " dB"
                          + "   " + note.name + juce::String (note.octave);

    const auto width = juce::GlyphArrangement::getStringWidth (t.labelFont(), text) + 20.0f;

    auto box = juce::Rectangle<float> (width, 26.0f)
                   .withCentre ({ (float) cursorPosition.x, plot.getY() + 60.0f });

    box.setX (juce::jlimit (plot.getX() + 2.0f, plot.getRight() - width - 2.0f, box.getX()));

    g.setColour (t.background.withAlpha (0.92f));
    g.fillRoundedRectangle (box, 4.0f);

    g.setColour (t.accent);
    g.drawRoundedRectangle (box, 4.0f, 1.0f);

    g.setColour (t.text);
    g.setFont (t.labelFont());
    g.drawText (text, box, juce::Justification::centred, false);
}

//==============================================================================
void SpectrumPage::mouseMove (const juce::MouseEvent& event)
{
    cursorPosition = event.getPosition();
    cursorInside = true;

    if (cursorButton.getToggleState())
        repaint();
}

void SpectrumPage::mouseExit (const juce::MouseEvent&)
{
    cursorInside = false;
    repaint();
}
