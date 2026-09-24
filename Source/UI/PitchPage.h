//
//  PitchPage.h
//  FX Analyzer
//
//  The note, the cents, and how much the detector believes itself.
//
//  Confidence is on the panel rather than hidden behind a threshold, because
//  the alternative was tried and is worse. A tuner that simply blanks below a
//  threshold gives no way to tell "no note here" from "the note is there and
//  the detector is struggling", and on a mix bus - which is where this plugin
//  usually sits - the second case is most of them. Showing the number lets the
//  reader discount a reading rather than never see it.
//
//  The cents bar is symmetrical and the centre is wide. Fifty cents either side
//  is the full width, so a semitone of error fills the bar, and the in-tune
//  band is +/- 5 cents, which is roughly where a good ear stops hearing a
//  difference on a sustained note.
//

#pragma once

#include "PageBase.h"
#include "StepperControl.h"

class PitchPage : public PageBase
{
public:
    explicit PitchPage (FXAnalyzerProcessor&);

    void refresh() override;
    void pageShown() override;

    void paint (juce::Graphics&) override;
    void resized() override;

protected:
    void themeChanged() override;

private:
    void syncFromState();
    void paintCentsBar (juce::Graphics&, juce::Rectangle<float>, float cents, bool haveNote) const;

    StepperControl referenceStepper;

    juce::StringArray referenceNames;
    int referenceIndex = 0;

    bool syncing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchPage)
};
