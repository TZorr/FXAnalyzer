//
//  StereoPage.h
//  FX Analyzer
//
//  Goniometer, correlation, balance, width.
//
//  The goniometer is drawn rotated 45 degrees, so that a mono signal is a
//  vertical line and a wide one spreads horizontally. Unrotated it would be a
//  diagonal, which is correct and useless: the eye reads "upright" as normal
//  and "leaning" as a problem, and a display whose healthy state looks like a
//  fault is a display people learn to ignore.
//
//  Points, not lines, by default. Joining consecutive samples draws the path
//  the signal took between them, which is prettier and is an interpolation
//  nobody measured. Lines mode exists because on a sparse signal - a single
//  sine, a test tone - the path is the whole point and the dots are too few to
//  see.
//
//  This page ignores the Channel selector, and says so on its face. A
//  goniometer of "Left only" is a vertical line and a correlation of a channel
//  against itself is +1 for ever; both are correct answers to a question nobody
//  asked, and silently showing them would make the page look broken.
//

#pragma once

#include "PageBase.h"
#include "StepperControl.h"

class StereoPage : public PageBase
{
public:
    explicit StereoPage (FXAnalyzerProcessor&);

    void refresh() override;
    void pageShown() override;

    void paint (juce::Graphics&) override;
    void resized() override;

protected:
    void themeChanged() override;

private:
    void syncFromState();
    void collectPoints();

    void paintGoniometer (juce::Graphics&, juce::Rectangle<float>) const;
    void paintCorrelation (juce::Graphics&, juce::Rectangle<float>) const;
    void paintReadouts (juce::Graphics&, juce::Rectangle<float>) const;

    StepperControl modeStepper, zoomStepper;

    int modeIndex = 0;
    int zoomIndex = 0;

    std::vector<float> leftSamples, rightSamples;
    int collected = 0;

    bool syncing = false;

    /** How many samples the cloud is drawn from. 4096 at a 30 Hz refresh covers
        about three frames' worth at 48 kHz, so the picture is dense enough to
        show a shape without lagging behind what is being heard. */
    static constexpr int cloudSize = 4096;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoPage)
};
