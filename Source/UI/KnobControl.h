//
//  KnobControl.h
//  FX Analyzer
//
//  Rackbox's knob, for the two settings that are continuous: Tilt Slope and
//  Input Gain. Everything else on the Settings page is a choice from a list and
//  stays a stepper; a knob would hide which of eight entries is set behind a
//  pointer angle.
//
//  Drawn the way Knob::paint draws it in Rackbox - a 270 degree track from
//  seven o'clock, the set range in the accent (from the middle when the knob is
//  bipolar), a flat face with a hairline edge and an ink pointer - with the
//  colours taken from Theme rather than written here, which is the one rule
//  this panel has about colour.
//
//  A juce::Slider rather than a ThemedComponent, so it keeps the Slider's drag,
//  wheel and double-click-to-reset behaviour. It cannot be both, so it holds a
//  Theme of its own; the design is fixed, so that is the same Theme every other
//  component holds.
//

#pragma once

#include "PageBase.h"

class KnobControl : public juce::Slider
{
public:
    KnobControl (const juce::String& labelText, bool isBipolar);

    void paint (juce::Graphics&) override;

    /** Dial, then the name, then the value. */
    static constexpr int labelHeight = 26;

private:
    juce::String label;
    bool bipolar;
    Theme palette;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobControl)
};
