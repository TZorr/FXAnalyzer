//
//  TargetPage.h
//  FX Analyzer
//
//  How far the input is from Clearbox's target curve: a rounded 4.5 dB/oct
//  slope, flat from 300 Hz to 2 kHz in SPAN's 4.5 dB/oct view. It is the same
//  view as Clearbox's Analyze mode, for matching the curve with an external
//  EQ.
//
//  The numbers are Clearbox's own. The measurement is Clearbox's engine,
//  copied verbatim into Source/Target and run with every knob at rest:
//  - a 2 s average in dB per ERB band, rising in half the time it falls,
//    longer in the bass;
//  - a global gate (HOLD below about -55 dBFS);
//  - a confidence gate, so a band far below the curve counts as missing.
//  Both plugins show the same values for the same signal.
//
//  The curve: the deviation per band, above the line too loud (accent),
//  below too quiet (blue). Below the lowest band (~47 Hz) it is carried on to
//  20 Hz with the slope of the two lowest bands, at most 12 dB. Underneath is
//  one value per octave from 63 Hz to 16 kHz: the confidence-weighted mean of
//  the bands within half an octave, rounded to 0.5 dB, or a dash where the
//  band is missing.
//
//  The channel selection applies. Freeze holds the picture.
//

#pragma once

#include <array>

#include "PageBase.h"
#include "../Target/ClearEngine.h"

class TargetPage : public PageBase
{
public:
    explicit TargetPage (FXAnalyzerProcessor&);

    void refresh() override;
    void pageShown() override;
    void paint (juce::Graphics&) override;

    static constexpr int numOctaves = 9;
    static constexpr std::array<float, numOctaves> octaveHz { 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };

    /** The octave values as shown, for the checks. */
    float octaveDeviationDb (int i) const noexcept { return octaveDb[(size_t) i]; }
    bool octaveHasSignal (int i) const noexcept    { return octaveValid[(size_t) i]; }

    static constexpr float rangeDb = 15.0f;

private:
    juce::Rectangle<float> displayArea() const;
    float splineAt (float t) const noexcept;   // t: 0 at 20 Hz ... 1 at 20 kHz

    int bands = 0;
    std::array<float, ClearEngine::maxBands> hz {}, deviation {}, confidence {};
    std::array<float, numOctaves> octaveDb {};
    std::array<bool, numOctaves> octaveValid {};
    bool holding = false;
    double lastRefreshMs = 0.0;

    static constexpr int gridPoints = 300;
    static constexpr float glideSeconds = 0.15f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TargetPage)
};
