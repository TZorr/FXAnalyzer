//
//  IconButton.h
//  FX Analyzer
//
//  A small flat button with a word on it: the cursor readout, the bass zoom
//  and freeze in the corner of a graph, and Reset on the Loudness page.
//
//  Until 0.2 these were circles with drawn symbols in them - a magnifier, a
//  snowflake, a letter B, a circular arrow. The name stayed because the enum
//  does: the four buttons are still four kinds, and each kind now has a word.
//

#pragma once

#include "PageBase.h"

class IconButton : public ThemedComponent,
                   public juce::SettableTooltipClient
{
public:
    /** Which button this is; see text() in the .cpp for the word each gets. */
    enum class Icon { magnifier, snowflake, bass, reset, word };

    explicit IconButton (Icon iconToDraw);

    /** A button with any word on it - the channel buttons beside these. */
    explicit IconButton (const juce::String& word);

    /** Where the button sits. On a graph's dark bed it takes the bed's own
        button colours; on the panel body (Loudness's Reset) the body's. */
    void setOnBed (bool shouldBeOnBed) { onBed = shouldBeOnBed; repaint(); }

    /** A toggle stays lit once clicked; a momentary button flashes and returns.
        Freeze is a toggle, Reset is not. */
    void setToggleMode (bool shouldToggle) { toggles = shouldToggle; }

    /** A toggle that shows its state with a lamp before the word instead of
        lighting up whole (0.4.0: Cursor, Bass and Freeze on a graph). The
        segmented channel buttons still light up whole - one of them always is. */
    void setShowsLamp (bool shouldShowLamp) { showsLamp = shouldShowLamp; repaint(); }

    /** The width the button wants for its word: padding, lamp and text. */
    int getIdealWidth() const;

    void setToggleState (bool shouldBeOn);
    bool getToggleState() const noexcept { return on; }

    std::function<void (bool)> onClick;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::String text() const;

    Icon icon;
    juce::String customText;
    bool toggles = true;
    bool on = false;
    bool hovering = false;
    bool onBed = true;
    bool showsLamp = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};
