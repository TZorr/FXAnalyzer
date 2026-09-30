//
//  PageBase.h
//  FX Analyzer
//
//  The two things every piece of this panel has in common: it knows the palette,
//  and it is told when to look at the analysis again.
//
//  ThemedComponent exists so that no component ever reads a colour from
//  anywhere but the theme it was handed. It is a two-line class and it is the
//  enforcement mechanism for the rule Theme.h argues for - a component that
//  does not have a theme cannot paint, so there is no path by which a widget
//  quietly acquires a colour of its own.
//
//  PageBase adds the refresh tick. Pages are refreshed only while visible,
//  which is what makes it safe for a page to do real work in refresh(): the
//  goniometer walks several thousand samples per frame and would be a waste of
//  a core on every frame the Loudness page is open.
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../ParameterIds.h"
#include "../Theme.h"

class FXAnalyzerProcessor;

class ThemedComponent : public juce::Component
{
public:
    ThemedComponent() = default;

    void setTheme (const Theme& newTheme)
    {
        currentTheme = newTheme;

        // Down the tree automatically. Pages used to hand the theme to their
        // children by name, and the Spectrum page's list was written when it
        // had three steppers - so the two added later kept the default theme
        // and stayed green while the rest of the panel went amber. A list that
        // has to be updated by hand every time a child is added is a list that
        // will be out of date, and nothing about it is visible until somebody
        // switches theme.
        for (auto* child : getChildren())
            if (auto* themed = dynamic_cast<ThemedComponent*> (child))
                themed->setTheme (newTheme);

        themeChanged();
        repaint();
    }

    const Theme& theme() const noexcept { return currentTheme; }

protected:
    /** Override to push the theme into child components. Called before the
        repaint, so a child can be resized in response to a font size change. */
    virtual void themeChanged() {}

private:
    Theme currentTheme;
};

//==============================================================================
class PageBase : public ThemedComponent
{
public:
    explicit PageBase (FXAnalyzerProcessor& processor) : plugin (processor) {}

    /** Called from the editor's timer, at the panel's frame rate, only while
        this page is the visible one. */
    virtual void refresh() {}

    /** Called when the page becomes visible, before the first refresh. A page
        that caches anything from the view state re-reads it here. */
    virtual void pageShown() {}

protected:
    FXAnalyzerProcessor& plugin;
};

//==============================================================================
namespace Layout
{
    // The panel's proportions, in the units the mockup was drawn in. Everything
    // else is derived from the area left over, so the panel scales as one piece
    // rather than as a header that grows and a graph that does not.
    inline constexpr int defaultWidth  = 900;
    inline constexpr int defaultHeight = 504;
    inline constexpr int headerHeight  = 68;
    inline constexpr int tabBarHeight  = 34;

    /** The space between a page's edge and what it draws, so that the displays
        and the header line up at the panel's 15 point margin (the editor itself
        is inset by 2). */
    inline constexpr int pageMarginX = 13;
    inline constexpr int pageMarginY = 6;

    /** Dead space below the tab labels, at the very bottom of the window.

        Logic does not reliably deliver a click that lands in the last few
        points of a plugin window. This is not a guess: the panel records every
        mouse event it receives, and a capture from Logic shows eleven seconds
        with the pointer inside the tab strip, several clicks made in that time
        by the user, and not one mouse-down arriving. Clicks a little higher in
        the same strip arrive every time - the display was never frozen either,
        the frame and paint counters ran at 27 per second throughout.

        Nothing here can make the host deliver those events. What it can do is
        keep the labels out of the band where they are lost. The strip is drawn
        its full height and stays clickable to the last pixel - a click that
        does arrive down there still works - but the labels, and the mark under
        the selected one, sit above this margin. EditorShot holds their distance
        from the window edge to a floor. */
    inline constexpr int tabBarSafeBottom = 14;

    /** The column of steppers down the right of the graph pages. Every point
        taken off it goes to the graph, which is the only thing on these pages
        anybody is actually looking at - so it is as narrow as the longest label
        it has to hold, and no narrower. That label is "4.5 dB/oct". */
    inline constexpr int sideColumnWidth = 92;

    /** JUCE's resize grip, from AudioProcessorEditor::editorResized. It sits on
        the bottom right of the editor, always on top, which puts it on the tab
        strip - the strip is shorter than the grip is tall. The tabs run under
        it rather than around it, because the grip only claims a triangle in the
        corner and reserving the whole column loses more than it saves; the hit
        map is what says so and what would notice if the grip grew. */
    inline constexpr int resizeGripSize = 18;

    /** Room for the dB labels down the left of a graph, and for the frequency
        labels under it. */
    inline constexpr int axisGutterLeft   = 56;
    inline constexpr int axisGutterBottom = 26;
}
