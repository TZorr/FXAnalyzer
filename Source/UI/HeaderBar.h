//
//  HeaderBar.h
//  FX Analyzer
//
//  Name, a display, preset and menu - the top row of Kitbox and Rackbox: the
//  wordmark on the left, a near-black display saying which page is open and
//  what the input is, and the two buttons on the right.
//
//  There was an input-activity lamp to the left of the name, and it is gone by
//  request. What went with it is the one glance that separated "silent bus"
//  from "bus the host never routed here" - a spectrum lying flat on the floor
//  looks identical in both cases. setActivity() is kept and still called, so
//  the level is there for whatever indicates it next; it simply draws nothing
//  today.
//

#pragma once

#include "PageBase.h"

class HeaderBar : public ThemedComponent
{
public:
    HeaderBar();

    /** The page's name and one line saying what it shows, for the display in
        the middle of the header. */
    void setPage (const juce::String& name, const juce::String& description);

    /** The right-hand side of the display's top line: sample rate and channel,
        and FROZEN while the analysis is held. Repaints only on a change. */
    void setStatus (const juce::String& text, bool frozen);

    void setPresetName (const juce::String&);

    /** 0 to 1, from the input's recent level. Smoothed by the caller. */
    void setActivity (float level);

    std::function<void()> onPresetClicked;
    std::function<void()> onMenuClicked;

    /** Where the two controls actually are, in screen coordinates.

        A popup targeted at the header as a whole is placed against a strip the
        full width of the panel, so it opens at the far left - a menu belonging
        to a control in the top right corner, appearing in the opposite corner.
        Both menus are anchored to their own control instead. */
    juce::Rectangle<int> getPresetScreenArea() const { return localAreaToGlobal (presetArea); }
    juce::Rectangle<int> getMenuScreenArea() const   { return localAreaToGlobal (menuArea); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::String pageName, pageDescription, status;
    bool frozen = false;
    juce::String presetName { "Default" };
    float activity = 0.0f;

    juce::Rectangle<int> wordmarkArea, displayArea, presetArea, menuArea;
    bool hoveringPreset = false, hoveringMenu = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};
