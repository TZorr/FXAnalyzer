//
//  TabBar.h
//  FX Analyzer
//
//  Six page names across the bottom.
//
//  The active tab is the pale one and the others are the accent colour, which
//  is the opposite of the usual convention and is what the mockup does. It is
//  also right for this panel: the accent is the colour of every curve on the
//  screen, so an accent-coloured active tab would be the one element competing
//  with the data for the same colour. The pale tab wins by contrast against a
//  row of green instead.
//
//  Against a row of *green*. That was the whole of the argument and it did not
//  survive the themes it was meant to serve: in Onyx the accent is a near-white
//  and so is the text, and the measured contrast between the selected tab and
//  the others came out at 1.00 - the same brightness, to two decimal places.
//  The strip showed nothing about which page was open. It was reported as
//  clicks not working, which is what it looks like from the outside: the page
//  did change, every time, and the only thing that said so was the graph.
//
//  So the difference no longer rests on the palette. That was tried in stages
//  and the stages are worth recording, because the first two were not enough.
//
//  Dimming the unselected tabs fixed the measurement and not the panel: the
//  *hovered* tab was still drawn in the text colour, so the tab under the
//  pointer looked exactly like the selected one - 1.14 apart in Yutani - and
//  since the pointer is always on the tab you are about to click, the thing you
//  were looking at was already bright before you clicked it. Nothing appeared
//  to happen. Reported as having to click beside the word instead of on it,
//  which is what you try when a click seems not to register.
//
//  Retuning the alphas cannot fix that either, and the numbers say why: in a
//  monochrome theme the accent and the text are the same colour, so three
//  states cannot be told apart by brightness. At the point where hover is
//  visible against the unselected tabs it is indistinguishable from selected.
//
//  So the selection is a *shape*: a bar under the label. Hover is the same bar,
//  faint. Presence and strength of a mark, rather than a shade of a colour -
//  which is the one thing a palette cannot take away. The bar is drawn in the
//  text colour, the one colour a theme cannot make illegible without making the
//  whole panel illegible; Ice's accent measured 2.93 against its own bed, under
//  the 3:1 that a piece of interface which is not text has to clear.
//

#pragma once

#include "PageBase.h"

class TabBar : public ThemedComponent
{
public:
    TabBar();

    void setTabs (const juce::StringArray&);
    void setSelectedIndex (int index, juce::NotificationType notify = juce::sendNotification);


    int getSelectedIndex() const noexcept { return selectedIndex; }

    /** The height the labels are centred in, measured from the top of the
        strip. Whatever is left below is drawn but never written into - see
        Layout::tabBarSafeBottom for why that dead space has to exist.

        Zero means the whole height, which is what a bare TabBar does. Note
        that this moves the *labels* only: the strip stays clickable to its
        last pixel, because a click that does arrive down there should still
        work. */
    void setContentHeight (int pixels);

    /** The three states, as the numbers that draw them. Public because the
        contrast check in EditorShot has to apply the same ones: a floor
        asserted against a different value than the one being drawn is not a
        floor.

        `inactiveAlpha` and `hoverAlpha` scale the accent for the label;
        `hoverMarkAlpha` scales the text colour for the bar, which the selected
        tab draws at full strength. */
    static constexpr float inactiveAlpha  = 0.6f;
    static constexpr float hoverAlpha     = 0.85f;
    static constexpr float hoverMarkAlpha = 0.32f;

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Which tab covers a point, or -1. Public because the hit map in
        EditorShot sweeps every pixel of the strip through it: the question
        "does the click area agree with the label drawn there" is one only a
        machine has the patience to ask a hundred thousand times. */
    int indexAt (juce::Point<int>) const;

    /** The bottom of the row the labels are drawn in, measured from the top of
        the strip. What lies below is the safe margin; EditorShot checks how far
        that keeps the labels from the bottom of the window. */
    int getContentBottom() const noexcept
    {
        return contentHeight > 0 ? juce::jmin (contentHeight, getHeight()) : getHeight();
    }

private:
    /** Where the label is drawn. */
    juce::Rectangle<float> areaForTab (int index) const;

    /** Where a click counts: the drawn rectangle everywhere except at the two
        ends, where the first and last tabs run out to the edges of the strip.

        A twelve-pixel margin at each end used to belong to no tab at all -
        clicks there did nothing, and nothing on screen said the strip had edges
        that were not part of it. The hit target may be larger than the thing
        drawn; it may never be smaller.

        Reserving room on the right for JUCE's resize grip was tried first and
        was worse, which the hit map showed and nothing else would have: the
        grip claims only a small triangle in the very corner, so holding the
        tabs off the whole 18-point column left 497 dead pixels where there had
        been 151 live ones. The grip keeps its triangle; the last tab gets the
        rest. */
    juce::Rectangle<float> hitAreaForTab (int index) const;

    juce::StringArray tabs;
    int selectedIndex = 0;
    int hoverIndex = -1;
    int contentHeight = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabBar)
};
