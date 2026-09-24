//
//  SettingsPage.h
//  FX Analyzer
//
//  Every setting on the panel, grouped by the question it answers, over a live
//  picture of what it does.
//
//  The page used to be nine large steppers in two rows, ordered by whether a
//  setting changed the measurement or only the presentation. That split was
//  right about the plugin and wrong about the person using it: somebody tuning
//  the spectrum until it reads musically is not asking "is this a measurement
//  setting?", they are asking "how fine is it, how fast is it, how is it
//  framed?" - and the answer to each was spread across two pages, with Top,
//  Range and Slope on the Spectrum page and Attack on this one. Rebuilt on
//  2026-09-24 around six groups, named after those questions:
//
//      RESOLUTION   Mode (Multi/Single), FFT Size, Bands
//      RANGE        Top, Range
//      DISPLAY      View (2D/Bars/Sonogram), Scale (Log/Lin), Theme
//      SMOOTHING    Reactivity, Attack, Release
//      TILT         Slope, Pivot
//      INPUT        Channel, DC Block, Input Gain
//
//  Two rows of eight, each group titled, all at the compact size. The groups
//  are ordered 3-2-3 in both rows so their edges line up down the page - a
//  grid of sixteen reads as two tidy rows, one of ragged edges reads as noise.
//  The large steppers were the reason the page could hold nine controls and
//  nothing else; compact ones hold sixteen and leave half the height for the
//  thing that makes the page worth having.
//
//  That thing is the preview along the top: the real SpectrumPage, embedded,
//  reading the same view state. Tuning a display by ear means stepping a value
//  and watching the curve answer, and a settings page that hides the curve
//  turns every step into a trip to another tab and back. Drawing it with the
//  Spectrum page's own class rather than a simplified copy is the point: what
//  the preview shows is what the Spectrum tab will show, down to the tier seam.
//
//  Since the same day the Spectrum page has no controls column at all: View,
//  Scale, Slope, Top and Range were there and are here now, so every setting
//  has exactly one place and that place shows its effect.
//
//  "Bands" is the fractional-octave smoothing, renamed. It is the width of the
//  band each point on the curve averages over - 1/24 octave is 24 bands to the
//  octave - which is what the control does, while "Smoothing" now names the
//  group that sets how the curve moves in time. The property underneath is
//  unchanged, so no session notices.
//
//  Nothing here is a preference hidden in a submenu. A setting that alters the
//  numbers has to be visible from the page that shows them, which is why the
//  Spectrum page still repeats the sample rate and the FFT size along its top
//  edge.
//

#pragma once

#include "PageBase.h"
#include "SpectrumPage.h"
#include "StepperControl.h"

class SettingsPage : public PageBase
{
public:
    explicit SettingsPage (FXAnalyzerProcessor&);

    void refresh() override;
    void pageShown() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The editor rebuilds itself when this fires, because a theme change
        touches every open component and the page cannot reach them. */
    std::function<void()> onThemeSelected;

protected:
    void themeChanged() override;

private:
    void syncFromState();

    /** After any change the preview has to see: re-reads the view state it
        caches. Cheap, and deliberately called for every control rather than
        only the ones known to matter today - the list of settings the preview
        depends on is the list that grows. */
    void updatePreview();

    /** One titled group of controls. The title and its rule are painted by
        the page; the group only records where they go. */
    struct Group
    {
        juce::String title;
        std::vector<StepperControl*> controls;
        juce::Rectangle<int> titleArea;
    };

    std::vector<StepperControl*> allSteppers();

    SpectrumPage preview;

    StepperControl resolutionStepper, fftSizeStepper, bandsStepper;
    StepperControl topStepper, rangeStepper;
    StepperControl slopeStepper, pivotStepper;
    StepperControl reactivityStepper, attackStepper, releaseStepper;
    StepperControl channelStepper, dcBlockStepper, inputGainStepper;
    StepperControl viewStepper, scaleStepper, themeStepper;

    std::vector<Group> topRow, bottomRow;

    bool syncing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPage)
};
