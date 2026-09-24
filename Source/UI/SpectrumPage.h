//
//  SpectrumPage.h
//  FX Analyzer
//
//  The page the plugin gets opened for.
//
//  Everything drawn here goes through one intermediate array: one dB value per
//  horizontal pixel, built once per frame. The curve, the bars and the sonogram
//  then all read from it. That is not a tidiness measure - it is what keeps the
//  three modes agreeing with each other. Drawn independently they disagree at
//  the top end, where a pixel spans a dozen bins and the answer depends
//  entirely on whether the mode took the maximum, the mean or the nearest bin,
//  and a user switching from 2D to Bars sees the peak move.
//
//  Taking the maximum is the choice, and it is the only defensible one for a
//  display whose job is to find the resonance. The mean of twelve bins hides a
//  narrow spike inside a quiet neighbourhood, which is exactly the thing being
//  looked for.
//
//  The slope control tilts the drawn curve without touching the measurement.
//  Music falls at roughly 3 to 4.5 dB per octave, so the honest spectrum of a
//  finished master is a diagonal, and comparing two diagonals by eye is much
//  harder than comparing two horizontals. The tilt is applied at draw time and
//  the axis is left alone.
//
//  The same goes for the octave smoothing: both are ways of making the curve
//  readable, and neither may reach the cursor readout. That readout goes back
//  to the raw spectrum, because it is the number somebody is about to dial into
//  an equaliser - it has to be what was measured, not what was drawn, and it
//  must not move when a display setting changes. This was wrong once: the
//  readout was taken from the drawn curve and quietly included the tilt.
//
//  Where the readout does follow a setting is in time. It used to read the
//  instantaneous spectrum - one frame, no averaging - and on music that number
//  moved by twenty decibels between repaints, so it could be watched but not
//  read. It now reads the time-smoothed curve, which means Reactivity governs
//  how still it stands: the same control that calms the display calms the
//  number, and at Very Slow it is effectively static. That is a measurement
//  setting rather than a cosmetic one, which is why it is allowed in where the
//  tilt and the octave smoothing are not.
//
//  Reading it also has to start by asking which tier owns the frequency. It did
//  not, and that was a real fault: the readout took tier zero - the shortest
//  window - while at 50 Hz in Multi the curve is drawn from the 65536-point
//  tier. Two different transforms, one cursor, and nothing on screen to say so.
//

#pragma once

#include "GraphAxes.h"
#include "IconButton.h"
#include "PageBase.h"
#include "StepperControl.h"

class SpectrumPage : public PageBase
{
public:
    explicit SpectrumPage (FXAnalyzerProcessor&);

    void refresh() override;
    void pageShown() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Turns the page into a preview: the graph and its axes, without the
        three buttons or the cursor.

        It exists for the Settings page, and it is this class rather than a
        second drawing of the spectrum because a preview that drew the curve its
        own way would be a preview of something else - the three display modes
        were given one column builder for exactly that reason. The embedded
        instance reads the same view state as the real page, so what Settings
        shows while a control is being stepped is what the Spectrum tab will
        show afterwards. */
    void setEmbedded (bool shouldBeEmbedded);

    /** "4.5 dB/oct", "3 dB/oct". Shared with the Settings page so the tilt is
        written the same way in both places it can be set. */
    static juce::String formatTilt (float dbPerOctave);

protected:
    void themeChanged() override;

private:
    /** Which numbers a tier is asked for.

        `drawn` is what the curve is made of: octave-smoothed across frequency.
        `measured` is what the cursor reports: the bins themselves, averaged
        over time but not over their neighbours.

        Neither needs a level correction any more, and the reason they do not is
        worth knowing. The smoothed curve used to sit lower the longer the
        window - a band average divides by the bins in the band, and there are
        more of them - so the tiers carried a constant each to line up. That
        compensation now happens per bin inside SpectrumAnalyser, against a
        fixed reference window, so every tier and every FFT Size already reads
        the same level. The raw bins never had the problem: a calibrated tone
        reads its own level at any window length, measured to within
        0.00004 dB. */
    enum class Source { drawn, measured };

    struct Reading
    {
        float levelDb = -140.0f;
        float peakDb  = -140.0f;
    };

    /** One tier's answer for a frequency span - the loudest bin under it, or an
        interpolation between two when the span is narrower than one bin. */
    Reading sampleTier (int tierIndex, float lowHz, float highHz, Source) const;

    /** The same, from whichever tier owns the span, blended across the seam.
        The one place that decides which transform answers for a frequency, so
        that the curve and the cursor cannot come to different conclusions. */
    Reading readTiers (float lowHz, float highHz, Source) const;

    void rebuildColumns();
    void appendSonogramRow();
    void paintCurve (juce::Graphics&) const;
    void paintBars (juce::Graphics&, int barCount) const;
    void paintSonogram (juce::Graphics&) const;
    void paintCursorReadout (juce::Graphics&) const;
    void syncFromState();

    juce::Rectangle<float> plotArea() const;

    FXParams::SpectrumMode   mode  = FXParams::SpectrumMode::curve;
    FXParams::FrequencyScale scale = FXParams::FrequencyScale::logarithmic;
    float slopeDbPerOctave = 0.0f;
    float tiltPivotHz      = 1000.0f;
    bool  embedded         = false;

    /** The dB axis. Held here rather than read from ParameterIds at each use,
        because every one of the six things drawn on this page - curve, peak
        line, bars, sonogram colours, grid and cursor - has to agree on it, and
        six independent lookups is six chances for one of them to be left
        behind on the old fixed range. */
    float topDb    = FXParams::spectrumTopDb;
    float bottomDb = FXParams::spectrumBottomDb;

    /** The frequency axis, held here for the same reason as the decibel axis
        above and with the same history behind it. Four places read it - the
        column builder twice, the grid, the cursor - and a bass zoom that
        reached three of them would draw a curve over a grid belonging to a
        different band. */
    float viewMinHz = FXParams::spectrumMinHz;
    float viewMaxHz = FXParams::spectrumMaxHz;
    bool  bassZoom  = false;

    IconButton     cursorButton { IconButton::Icon::magnifier };
    IconButton     bassButton   { IconButton::Icon::bass };
    IconButton     freezeButton { IconButton::Icon::snowflake };

    std::vector<float> columnDb, columnPeakDb;

    juce::Image sonogram;
    int  sonogramRow = 0;
    bool sonogramValid = false;

    juce::Point<int> cursorPosition;
    bool cursorInside = false;
    bool syncing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumPage)
};
