//
//  Theme.cpp
//  FX Analyzer
//

#include "Theme.h"

#include <cmath>

namespace
{
    /** Every colour of a panel in one row, so the eight panels below read as a
        table and a missing value is a compile error, not a silent default. */
    struct Panel
    {
        juce::uint32 background, section, text, label,
                     accent, onAccent, button, buttonHover, knobFace, knobEdge, knobTrack,
                     bed, grid, curve, curveAlt, bedText, bedDim, bedButton, bedButtonHover,
                     warning, below;
    };

    Theme apply (const Panel& p)
    {
        Theme t;
        t.background     = juce::Colour (p.background);
        t.section        = juce::Colour (p.section);
        t.text           = juce::Colour (p.text);
        t.label          = juce::Colour (p.label);
        t.accent         = juce::Colour (p.accent);
        t.onAccent       = juce::Colour (p.onAccent);
        t.button         = juce::Colour (p.button);
        t.buttonHover    = juce::Colour (p.buttonHover);
        t.knobFace       = juce::Colour (p.knobFace);
        t.knobEdge       = juce::Colour (p.knobEdge);
        t.knobTrack      = juce::Colour (p.knobTrack);
        t.bed            = juce::Colour (p.bed);
        t.grid           = juce::Colour (p.grid);
        t.curve          = juce::Colour (p.curve);
        t.curveAlt       = juce::Colour (p.curveAlt);
        t.bedText        = juce::Colour (p.bedText);
        t.bedDim         = juce::Colour (p.bedDim);
        t.bedButton      = juce::Colour (p.bedButton);
        t.bedButtonHover = juce::Colour (p.bedButtonHover);
        t.warning        = juce::Colour (p.warning);
        t.below          = juce::Colour (p.below);
        return t;
    }

    //                     background  section     text        label       accent      onAccent    button      btnHover    knobFace    knobEdge    knobTrack   bed         grid        curve       curveAlt    bedText     bedDim      bedButton   bedBtnHover warning     below
    // Orange: Kitbox's and Rackbox's palette (the light one is Theme's defaults).
    constexpr Panel orangeDark { 0xff161513, 0xff22201d, 0xffece7de, 0xff9d978d, 0xfff28a1e, 0xff2c2a27, 0xff2f2c28, 0xff3a3631, 0xff36332e, 0xff55504a, 0xff3f3b36, 0xff0c0b0a, 0xff2a2622, 0xffff8c2b, 0xffe9e5dd, 0xffff8c2b, 0xffb37c46, 0xff3a3631, 0xff4a453f, 0xffe04a4f, 0xff4fb3d1 };

    // Blue: navy displays, a yellow trace and a white peak hold. Light = "Ice", dark = "Navy".
    constexpr Panel blueLight  { 0xffc8d2e0, 0xffb9c4d4, 0xff14203a, 0xff44516a, 0xffffcc29, 0xff14203a, 0xffb5c1d3, 0xffa6b3c7, 0xffdfe6f0, 0xff8494ad, 0xff9aa8bd, 0xff0b1630, 0xff1e2b48, 0xffffd23f, 0xffffffff, 0xffffd23f, 0xff93a8ca, 0xff1d2a48, 0xff283759, 0xffff5a5f, 0xff4fb3d1 };
    constexpr Panel blueDark   { 0xff101a2e, 0xff17233b, 0xffe6ecf5, 0xff8fa0ba, 0xffffd23f, 0xff14203a, 0xff1b2843, 0xff24334f, 0xff22304d, 0xff3a4a6b, 0xff2c3a58, 0xff071022, 0xff1a2640, 0xffffd23f, 0xffffffff, 0xffffd23f, 0xff8ea3c6, 0xff1b2844, 0xff253455, 0xffff5a5f, 0xff4fb3d1 };

    // Mint: graphite and a mint trace. "below" turns blue-violet, a cyan would sit too close to the mint.
    constexpr Panel mintLight  { 0xffd3d8d6, 0xffc5cbc9, 0xff1f2624, 0xff4f5856, 0xff1fae90, 0xff0c1f1b, 0xffbcc3c1, 0xffaeb6b3, 0xffe4e8e6, 0xff8f9895, 0xffa3aba8, 0xff121516, 0xff232829, 0xff3ad6b4, 0xffe8ecea, 0xff3ad6b4, 0xff74a69a, 0xff262b2c, 0xff333939, 0xffff5a5f, 0xff8f8cff };
    constexpr Panel mintDark   { 0xff1a1d1e, 0xff232728, 0xffe4e8e7, 0xff929a99, 0xff3ad6b4, 0xff0c1f1b, 0xff262a2b, 0xff313637, 0xff2f3435, 0xff4c5455, 0xff3a4041, 0xff0b0d0e, 0xff1f2425, 0xff3ad6b4, 0xffe8ecea, 0xff3ad6b4, 0xff74a69a, 0xff252a2b, 0xff31383a, 0xffff5a5f, 0xff8f8cff };

    // Red: sand and a red accent. The red is taken, so "over the ceiling" is yellow here.
    constexpr Panel redLight   { 0xffe4ddd0, 0xffd8d0c1, 0xff2a2420, 0xff5e5549, 0xffc23a28, 0xfffff8f0, 0xffd3cbbd, 0xffc6bdae, 0xfff0ebe2, 0xffa3988a, 0xffb8ad9f, 0xff1f1b19, 0xff352e2a, 0xffff5a3c, 0xffefe7dc, 0xffff6a4d, 0xffbd8a76, 0xff3a3430, 0xff4a423d, 0xffffd23f, 0xff4fb3d1 };
    constexpr Panel redDark    { 0xff181412, 0xff231d1a, 0xffefe7dc, 0xffa2968a, 0xffe0533a, 0xff170d09, 0xff2b2420, 0xff362d28, 0xff352d28, 0xff574b44, 0xff40362f, 0xff0d0a09, 0xff2a2320, 0xffff5a3c, 0xffefe7dc, 0xffff6a4d, 0xffbd8a76, 0xff2e2723, 0xff3c332e, 0xffffd23f, 0xff4fb3d1 };
}

Theme Theme::make (Colourway colourway, bool dark)
{
    switch (colourway)
    {
        case Colourway::blue: return apply (dark ? blueDark : blueLight);
        case Colourway::mint: return apply (dark ? mintDark : mintLight);
        case Colourway::red:  return apply (dark ? redDark  : redLight);
        case Colourway::orange:
        default:              return dark ? apply (orangeDark) : Theme {};
    }
}

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
