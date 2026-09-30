//
//  KnobControl.cpp
//  FX Analyzer
//

#include "KnobControl.h"

KnobControl::KnobControl (const juce::String& labelText, bool isBipolar)
    : label (labelText.toUpperCase()), bipolar (isBipolar)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                         juce::MathConstants<float>::pi * 2.75f, true);
    setMouseDragSensitivity (220);
    setVelocityBasedMode (false);
    setWantsKeyboardFocus (false);
}

void KnobControl::paint (juce::Graphics& g)
{
    const auto& t = palette;

    auto bounds = getLocalBounds().toFloat();
    auto textArea = bounds.removeFromBottom ((float) labelHeight);

    const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 2.0f;
    const auto centre   = bounds.getCentre();
    const auto ringRadius = diameter * 0.5f - 1.5f;
    const auto faceRadius = ringRadius - 5.0f;

    const auto& rotary = getRotaryParameters();
    const auto proportion = (float) valueToProportionOfLength (getValue());
    const auto angle = rotary.startAngleRadians + proportion * (rotary.endAngleRadians - rotary.startAngleRadians);
    const auto zeroAngle = bipolar ? (rotary.startAngleRadians + rotary.endAngleRadians) * 0.5f
                                   : rotary.startAngleRadians;

    // The ring: full travel in the track colour, the set range in the accent.
    {
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                             rotary.startAngleRadians, rotary.endAngleRadians, true);
        g.setColour (t.knobTrack);
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (std::abs (angle - zeroAngle) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                                 juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);
            g.setColour (t.accent);
            g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // The face: one flat disc and a hairline.
    g.setColour (isMouseOverOrDragging() ? t.knobFace.brighter (0.06f) : t.knobFace);
    g.fillEllipse (centre.x - faceRadius, centre.y - faceRadius, faceRadius * 2.0f, faceRadius * 2.0f);
    g.setColour (t.knobEdge);
    g.drawEllipse (centre.x - faceRadius, centre.y - faceRadius, faceRadius * 2.0f, faceRadius * 2.0f, 1.0f);

    // The pointer.
    {
        const auto inner = juce::Point<float> (centre.x + faceRadius * 0.25f * std::sin (angle),
                                               centre.y - faceRadius * 0.25f * std::cos (angle));
        const auto outer = juce::Point<float> (centre.x + faceRadius * 0.82f * std::sin (angle),
                                               centre.y - faceRadius * 0.82f * std::cos (angle));
        g.setColour (t.text);
        g.drawLine ({ inner, outer }, 2.5f);
    }

    // Name and value.
    g.setColour (t.label);
    g.setFont (t.labelFont());
    g.drawFittedText (label, textArea.removeFromTop (12.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);

    g.setColour (t.text);
    g.setFont (t.valueFont());
    g.drawFittedText (getTextFromValue (getValue()), textArea.toNearestInt(), juce::Justification::centred, 1, 0.8f);
}
