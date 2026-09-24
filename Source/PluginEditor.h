//
//  PluginEditor.h
//  FX Analyzer
//
//  The frame: header at the top, page in the middle, tabs along the bottom.
//
//  One timer for the whole panel, at 30 Hz, and it does two things in a fixed
//  order - tell the AnalysisHub to pull new windows out of the capture ring,
//  then tell the visible page to redraw. Both halves being on the same tick is
//  what stops the pages disagreeing: two timers would let the Spectrum page
//  draw a frame the hub had not produced yet, and the resulting stutter is
//  indistinguishable from a dropout in the audio.
//
//  Only the visible page is refreshed. Pages are all constructed up front and
//  kept alive - six components is nothing, and rebuilding one on every tab
//  change would throw away the sonogram's history and the goniometer's cloud
//  every time somebody looked at the loudness for a second.
//
//  Thirty hertz, not sixty. A spectrum analyser at 60 Hz costs twice the CPU to
//  show the same thing: the smoothing time constants are all tens of
//  milliseconds, so the extra frames are interpolating between values that have
//  not changed. The one display that would benefit is the scope, and a
//  triggered scope is stationary by construction.
//

#pragma once

#include "PluginProcessor.h"

#include "UI/ColourEditor.h"
#include "UI/HeaderBar.h"
#include "UI/LoudnessPage.h"
#include "UI/PitchPage.h"
#include "UI/ScopePage.h"
#include "UI/SettingsPage.h"
#include "UI/SpectrumPage.h"
#include "UI/StereoPage.h"
#include "UI/TabBar.h"

class FXAnalyzerEditor : public juce::AudioProcessorEditor,
                         private juce::Timer,
                         private juce::ChangeListener
{
public:
    explicit FXAnalyzerEditor (FXAnalyzerProcessor&);
    ~FXAnalyzerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool keyPressed (const juce::KeyPress&) override;

    /** Every click anywhere in the editor arrives here, through the nested
        mouse listener registered in the constructor - including the ones a
        child component received. It only records; see getDiagnostics.

        Public because it overrides a public method and because EditorShot calls
        it: the delivery cannot be exercised without a real mouse, so the least
        that can be tested is the body it delivers to. */
    void mouseDown (const juce::MouseEvent&) override;

    /** Recorded alongside the press, because a press and a release that go to
        different places, or a press with no release, look identical from
        outside and completely different here. */
    void mouseUp (const juce::MouseEvent&) override;

    /** Only the crossings in and out of the tab strip, not every move - enough
        to show that the pointer was over a tab at a moment when no click was
        recorded, which is the one shape that says a click never arrived. */
    void mouseMove (const juce::MouseEvent&) override;

    /** Renders the panel with a given page selected. EditorShot calls this;
        nothing else should. */
    void selectPage (int index);



    /** Which page is on screen. For EditorShot: saving state correctly and
        *showing* the saved state are two different things, and only the second
        one is what a user calls "saving works". */
    int getVisiblePageIndex() const;

    /** The recorded history, oldest first. Public so EditorShot can check that
        a synthesized click really does leave a trace - a diagnostic that is
        never exercised is a diagnostic that will be empty on the one day it
        matters. */
    juce::StringArray getDiagnostics() const;

    /** The theme the panel is currently painted in. For the hit map, which has
        to measure the label with the same font the strip draws it in. */
    const Theme& getPanelTheme() const { return theme; }

    /** The tab strip, for the hit map. A click is not delivered to the
        component you drew there, it is delivered to whatever getComponentAt
        finds - so the test has to be able to compare the two, and to name the
        thief when they differ. */
    const TabBar& getTabStrip() const { return tabs; }

    /** Changes how often the panel repaints. Public so EditorShot can measure
        what a given rate actually costs rather than leave it to be guessed. */
    void setRefreshRateHz (int hz);

    /** Everything one display frame does, minus the painting: advance the
        analysis, refresh the visible page, update the header. This is the
        timer's whole body, exposed so a frame's cost can be timed directly.

        Measuring it through the message loop instead was tried and was worse
        than useless - the loop's own dwell time set the pace, so the repaint
        rate under test had no effect on how much work happened and 60 Hz came
        out cheaper than 30. */
    void advanceOneFrame();

    /** Takes the colour editor back off the page. Public alongside
        showColourEditor for EditorShot: an overlay that outlives its shot
        covers whatever is rendered next, which is how the single-resolution
        comparison came out as a picture of the colour editor. */
    void closeColourEditor();

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void applyTheme();
    void unmuteStandaloneInput();
    void showMenu();
    void showColourEditor();
    void showPresetMenu();
    void importTheme();
    void exportTheme();
    void saveDiagnostics();

    PageBase* currentPage() const;

    //==============================================================================
    /** A running account of what the panel was asked to do, kept because the
        one fault this cannot reproduce is the one that only happens in a host.

        A click on the tab strip sometimes does nothing in Logic and has to be
        repeated. The strip's own geometry has been cleared by the hit map -
        every pixel reaches its tab - so what is left is either a click that
        never arrived or a page that was changed back by something else. Those
        two look identical from the outside and completely different here.

        It records always, rather than behind a switch. The fault is occasional:
        by the time anybody thinks to turn logging on, the interesting moment is
        several minutes in the past. Sixty-four entries is a couple of minutes
        of ordinary use and costs a few kilobytes.

        The mouse events arrive through addMouseListener with the nested flag
        set, so this sees every click anywhere in the editor along with the
        component that actually received it - including the ones a child took.
        A click that Logic swallows before it reaches the view appears here as
        nothing at all, which is itself the answer. */
    void record (const juce::String& what);

    void selectPage (int index, const char* reason);

    static constexpr int diagnosticCapacity = 256;

    std::array<juce::String, diagnosticCapacity> diagnostics;
    int diagnosticsWritten = 0;

    /** The page this editor last settled on, held here rather than read back
        from the tab strip. The strip is not a witness to its own change: it
        updates its index first and calls onChange second, so by the time
        selectPage runs the "before" value is already gone. The first version
        of this compared against it and recorded nothing at all - which the
        harness caught by asking the diagnostic to prove it had a history. */
    int lastPage = -1;

    /** Two counters that turn "nothing happened" into a fact.

        The first diagnostic from Logic showed every click arriving and every
        first click changing the page - so the click path was never the problem
        and the display was the thing to look at. These say whether the timer
        ran and whether the panel painted between one entry and the next: if
        they stand still across a burst of clicks, the screen was frozen; if
        they advance, it was not, and the fault is what the screen was showing. */
    int frameCount = 0;
    int paintCount = 0;

    /** Whether the pointer was last seen over the tab strip, so that only the
        crossings are recorded and not thirty positions a second. */
    bool pointerOnStrip = false;

    FXAnalyzerProcessor& plugin;

    HeaderBar header;
    TabBar    tabs;

    SpectrumPage spectrumPage;
    ScopePage    scopePage;
    LoudnessPage loudnessPage;
    StereoPage   stereoPage;
    PitchPage    pitchPage;
    SettingsPage settingsPage;

    std::array<PageBase*, FXParams::numPages> pages;

    Theme theme;

    std::unique_ptr<juce::FileChooser> chooser;

    /** A window of its own rather than a panel laid over the page.

        It used to cover the current page, which put it squarely on top of the
        thing whose colours were being chosen - you could not see the curve
        while picking the curve's colour. As a separate always-on-top window the
        panel stays visible and repaints live under every drag of the picker.

        Destroyed in this class's destructor without exception: a window that
        outlives its plugin editor holds a reference to a dead processor, and a
        host may tear an editor down at any moment. */
    std::unique_ptr<juce::DocumentWindow> colourWindow;

    ColourEditor* colourEditorContent() const;

    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FXAnalyzerEditor)
};
