//
//  Surfaces.h
//  FX Analyzer
//
//  The three surfaces of the panel, drawn the same way everywhere (0.4.0):
//
//  - a display (bed) is recessed into the body: a soft shadow along its top
//    edge inside, a dark rim, and a hairline of light along the body under
//    its bottom edge, where a real lip would catch it;
//  - a button stands on the body: a hairline of light along its top and a
//    hairline of shade under it; lit, it glows faintly in the accent;
//  - a lamp (LED) is a small dot in the accent, with the same glow, or a dim
//    dot when off.
//
//  The light and shade are the Theme's (bevelLight, bevelShade, innerShadow,
//  accentGlow), so the eight panels need no rule of their own here. All of it
//  is one or two pixels: the point is that the panel reads as a made object,
//  not that anybody notices a shadow.
//

#pragma once

#include "../Theme.h"

namespace Surfaces
{
    inline void recessed (juce::Graphics& g, const Theme& t, juce::Rectangle<float> area, float radius = 6.0f)
    {
        // The lip: light on the body just under the bottom edge.
        g.setColour (t.bevelLight());
        g.fillRoundedRectangle (area.translated (0.0f, 1.0f), radius);

        g.setColour (t.bed);
        g.fillRoundedRectangle (area, radius);

        // The shadow cast by the top edge onto the bed.
        juce::Path shape;
        shape.addRoundedRectangle (area, radius);

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (shape);
        g.setGradientFill (juce::ColourGradient (t.innerShadow(), area.getX(), area.getY(),
                                                 t.innerShadow().withAlpha (0.0f), area.getX(), area.getY() + 6.0f,
                                                 false));
        g.fillRect (area.withHeight (6.0f));

        g.setColour (t.bevelShade());
        g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
    }

    /** A button's fill with its light and shade. The caller draws the label. */
    inline void button (juce::Graphics& g, const Theme& t, juce::Rectangle<float> area,
                        juce::Colour fill, bool lit, bool onBed = false, float radius = 4.0f)
    {
        // No glow on a bed: there the lit fill is bright enough on the dark,
        // and a glow would be cut off flat at the small component's edges.
        if (lit && ! onBed)
        {
            juce::Path shape;
            shape.addRoundedRectangle (area, radius);
            juce::DropShadow (t.accentGlow(), 8, {}).drawForPath (g, shape);
        }
        else if (! onBed)
        {
            g.setColour (t.bevelShade());
            g.fillRoundedRectangle (area.translated (0.0f, 1.0f), radius);
        }

        g.setColour (fill);
        g.fillRoundedRectangle (area, radius);

        g.setColour (lit ? t.litHighlight() : (onBed ? t.bedHighlight() : t.bevelLight()));
        g.fillRect (area.getX() + radius, area.getY(), area.getWidth() - 2.0f * radius, 1.0f);
    }

    inline void lamp (juce::Graphics& g, const Theme& t, juce::Point<float> centre, bool on, float diameter = 6.0f)
    {
        const auto dot = juce::Rectangle<float> (diameter, diameter).withCentre (centre);

        if (on)
        {
            juce::Path shape;
            shape.addEllipse (dot);
            juce::DropShadow (t.accentGlow(), 6, {}).drawForPath (g, shape);
        }

        g.setColour (on ? t.accent : t.bedButtonHover);
        g.fillEllipse (dot);
    }
}
