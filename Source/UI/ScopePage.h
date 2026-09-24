//
//  ScopePage.h
//  FX Analyzer
//
//  The waveform, triggered, with both channels drawn over each other.
//
//  Two traces rather than two stacked graphs. Stacked, the question "are these
//  two channels the same" needs the eye to move between two boxes and remember
//  a shape; overlaid, the answer is whether the second colour is visible at
//  all. The same reasoning puts the goniometer on the Stereo page rather than a
//  pair of level meters.
//
//  Auto gain is the default and it names its own multiplier in the corner. An
//  auto-scaling display that does not say what it scaled by is a display whose
//  vertical axis means nothing, and the whole reason to look at a waveform is
//  to see how big it is.
//

#pragma once

#include "GraphAxes.h"
#include "IconButton.h"
#include "PageBase.h"
#include "StepperControl.h"

class ScopePage : public PageBase
{
public:
    explicit ScopePage (FXAnalyzerProcessor&);

    void refresh() override;
    void pageShown() override;

    void paint (juce::Graphics&) override;
    void resized() override;

protected:
    void themeChanged() override;

private:
    void syncFromState();
    void applyTimeBase();
    juce::Rectangle<float> plotArea() const;
    void paintTrace (juce::Graphics&, const std::vector<float>&, int count,
                     juce::Colour, float gain, float thickness) const;

    StepperControl timeStepper, triggerStepper, gainStepper;
    IconButton     freezeButton { IconButton::Icon::snowflake };

    int   timeIndex    = FXParams::defaultScopeTimeIndex;
    int   triggerIndex = 0;
    int   gainIndex    = FXParams::defaultScopeGainIndex;
    float appliedGain  = 1.0f;

    bool syncing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopePage)
};
