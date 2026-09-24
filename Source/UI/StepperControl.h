//
//  StepperControl.h
//  FX Analyzer
//
//  Label, up chevron, value, down chevron. The panel's only choice control.
//
//  A ComboBox would have been three lines instead of this file. It was rejected
//  for two reasons that both come from where this plugin is used. A combo box
//  opens a menu over the graph, which is the one part of the window somebody is
//  watching while they reach for the control; and its menu is a modal loop,
//  which in some hosts stops the editor's timer, so the display freezes for as
//  long as the menu is open and then jumps. A stepper changes the value in
//  place, under the finger, with the graph still moving.
//
//  It wraps at both ends. Five channels and three reactivities are short enough
//  lists that wrapping is faster than reversing direction, and long enough that
//  nobody loses their place.
//

#pragma once

#include "PageBase.h"

class StepperControl : public ThemedComponent
{
public:
    StepperControl();

    /** The label above the value. Empty draws no label and gives the value the
        space, which is what the compact steppers beside a graph want. */
    void setLabel (const juce::String& newLabel);

    /** Compact steppers set their value in the tab strip's type rather than the
        panel's large value type.

        The two sizes exist because the two positions are read differently. On
        the Settings page the value *is* the content, and it is the largest
        thing in its column. Beside a graph it is a caption on the graph, and
        set at the same size it competes with the data for attention while
        stealing the width the data needs. The tab strip is the right reference
        because the tabs are the other piece of chrome at the edge of the panel,
        and two edge labels at two sizes read as an accident. */
    void setCompact (bool shouldBeCompact);

    void setItems (const juce::StringArray& newItems);

    /** Switches the control from a list of choices to a continuous value.

        Input Gain is the reason this exists. It looks like the other steppers
        and must behave like them, but it is the one that is an automatable
        parameter with 0.1 dB resolution - and quantising it into a StringArray
        would snap every automation curve to the list's steps. That is a
        regression you only notice in a host, long after the change that caused
        it looked like a simplification.

        `step` is one click. Shift gives a tenth of it, and **alt-click** returns
        to `resetValue` - not double click, which JUCE delivers alongside the
        second mouseDown and which therefore fires during ordinary repeated
        clicking. */
    void setNumericRange (float minimum, float maximum, float step, float resetValue,
                          std::function<juce::String (float)> formatter);

    void setNumericValue (float newValue, juce::NotificationType notify = juce::sendNotification);
    float getNumericValue() const noexcept { return numericValue; }

    std::function<void (float)> onNumericChange;

    /** Wraps each change in the gesture a host expects around an automatable
        parameter. Set by the page that owns the control. */
    std::function<void()> onGestureStart, onGestureEnd;
    void setSelectedIndex (int index, juce::NotificationType notify = juce::sendNotification);

    int getSelectedIndex() const noexcept { return selectedIndex; }
    juce::String getSelectedText() const;

    /** The height at which the control's four rows sit tight against each
        other. Laying steppers out at a fixed height was fine while there was
        one size; with two it leaves the compact ones as a value floating in the
        middle of a box with its arrows pushed to the far edges, which reads as
        three unrelated things rather than as one control. */
    int getPreferredHeight() const;

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Zone { none, up, down };

    Zone zoneAt (juce::Point<int>) const;
    void step (int direction);
    void drawChevron (juce::Graphics&, juce::Rectangle<float> area, bool pointingUp, bool highlighted) const;

    /** The height the value is set at. Every other measurement in this
        component - chevron width, chevron height, stroke weight, row height -
        is a multiple of it, so that changing the size or the theme's type scale
        moves the whole control together instead of leaving arrows sized for the
        font it used to have. */
    float valueTextHeight() const;

    bool isNumeric() const noexcept { return numericStep > 0.0f; }

    juce::String label;
    juce::StringArray items;
    int selectedIndex = 0;
    bool compact = false;

    float numericValue = 0.0f, numericMin = 0.0f, numericMax = 1.0f;
    float numericStep = 0.0f, numericReset = 0.0f;
    std::function<juce::String (float)> numericFormatter;

    juce::Rectangle<int> labelArea, upArea, valueArea, downArea;
    Zone hoverZone = Zone::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepperControl)
};

//==============================================================================
/** Lays a column of steppers down the side of a graph page: each at its natural
    height, spread evenly over the space.

    One function rather than the same six lines in five pages. The first draft
    had them each doing their own arithmetic and they had already drifted - two
    pages used a 10 point gap and one used 12, which nobody would ever notice on
    one page and which makes the panel feel loose when you tab between them. */
void layOutStepperColumn (juce::Rectangle<int> column, const std::vector<StepperControl*>& steppers);
