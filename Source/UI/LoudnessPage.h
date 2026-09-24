//
//  LoudnessPage.h
//  FX Analyzer
//
//  Momentary, short-term, integrated, range, true peak - and the distance to a
//  target, which is the number anybody actually acts on.
//
//  The layout puts Integrated in the largest type, because it is the one that
//  decides whether a master ships, and the other four around it in the size
//  they are worth. Every meter here shows the same value twice, as a number and
//  as a position on a scale, and the reason is that the two are read for
//  different things: the number to write down, the bar to see whether it is
//  moving.
//
//  The integrated readout is dimmed until it rests on five seconds of gated
//  material. A LUFS number that has been averaging for half a bar is not wrong
//  so much as premature, and there is no way to tell that from the digits.
//

#pragma once

#include "IconButton.h"
#include "PageBase.h"
#include "StepperControl.h"

class LoudnessPage : public PageBase
{
public:
    explicit LoudnessPage (FXAnalyzerProcessor&);

    void refresh() override;
    void pageShown() override;

    void paint (juce::Graphics&) override;
    void resized() override;

protected:
    void themeChanged() override;

private:
    void syncFromState();

    void paintBar (juce::Graphics&, juce::Rectangle<float>, float lufs, juce::Colour) const;
    /** Caption above, number and unit side by side below. The unit is drawn
        separately and smaller because it is not part of the measurement: set in
        the same monospace as the digits it takes as much width as "-20.2" does
        and the eye reads the pair as one very wide number. */
    void paintReadout (juce::Graphics&, juce::Rectangle<float>, const juce::String& caption,
                       const juce::String& value, const juce::String& unit,
                       float textSize, juce::Colour) const;

    StepperControl targetStepper;
    IconButton     resetButton { IconButton::Icon::reset };

    int targetIndex = FXParams::defaultLoudnessTargetIndex;
    bool syncing = false;

    // The scale the bars are drawn on. -60 is far enough down that a quiet
    // stem still moves the bar; +6 leaves room above the loudest master anybody
    // will put through this.
    static constexpr float scaleTopLufs    =   6.0f;
    static constexpr float scaleBottomLufs = -60.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessPage)
};
