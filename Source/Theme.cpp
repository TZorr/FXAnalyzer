//
//  Theme.cpp
//  FX Analyzer
//

#include "Theme.h"

#include <cmath>

float Theme::contrastRatio (juce::Colour a, juce::Colour b)
{
    // sRGB to linear, per channel, then the Rec. 709 luminance weights.
    const auto luminance = [] (juce::Colour c)
    {
        const auto linear = [] (float v)
        {
            return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f);
        };

        return 0.2126f * linear (c.getFloatRed())
             + 0.7152f * linear (c.getFloatGreen())
             + 0.0722f * linear (c.getFloatBlue());
    };

    const auto la = luminance (a);
    const auto lb = luminance (b);

    return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
}
