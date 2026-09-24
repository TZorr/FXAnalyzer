//
//  IconButton.h
//  FX Analyzer
//
//  A circle with a symbol in it: the cursor readout and the freeze button that
//  sit in the corner of a graph.
//
//  The icons are drawn as paths rather than loaded as images. Not for purity -
//  because they are theme-coloured, and an image would have to be re-tinted, or
//  shipped once per theme, or drawn in a colour that stops matching the moment
//  the accent changes. A path takes the colour it is given.
//

#pragma once

#include "PageBase.h"

class IconButton : public ThemedComponent,
                   public juce::SettableTooltipClient
{
public:
    /** `bass` is drawn as the letter B rather than as a path. The reason for
        paths was never the geometry - it was that a themed icon must take the
        colour it is given, and a glyph does that as readily as a stroke. A
        picture of a low-pass shape would have been guesswork about what the
        button does; the letter is what was asked for and what the tooltip
        explains. */
    enum class Icon { magnifier, snowflake, bass, reset };

    explicit IconButton (Icon iconToDraw);

    /** A toggle stays lit once clicked; a momentary button flashes and returns.
        Freeze is a toggle, Reset is not. */
    void setToggleMode (bool shouldToggle) { toggles = shouldToggle; }

    void setToggleState (bool shouldBeOn);
    bool getToggleState() const noexcept { return on; }

    std::function<void (bool)> onClick;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void drawMagnifier (juce::Graphics&, juce::Rectangle<float>) const;
    void drawBass      (juce::Graphics&, juce::Rectangle<float>) const;
    void drawSnowflake (juce::Graphics&, juce::Rectangle<float>) const;
    void drawReset     (juce::Graphics&, juce::Rectangle<float>) const;

    Icon icon;
    bool toggles = true;
    bool on = false;
    bool hovering = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};
