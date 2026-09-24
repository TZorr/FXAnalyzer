//
//  GraphAxes.h
//  FX Analyzer
//
//  The grid, the axis labels and the two mappings every graph page needs:
//  frequency to x, decibels to y.
//
//  One file for all of it because the alternative was demonstrated by the first
//  draft, where the Spectrum page and the Scope page each grew their own grid.
//  They came out one pixel apart, with different label fonts and different
//  ideas about whether the top line counts as a gridline, and the panel looked
//  like two plugins sharing a window. A grid is furniture: it should be
//  identical everywhere or it is noticed everywhere.
//
//  The log mapping is the interesting one. Frequency is spaced by the logarithm
//  because pitch is, and the useful consequence is that an octave is the same
//  width wherever it lands - the two octaves between 40 Hz and 160 Hz occupy
//  the same screen space as the two between 4 kHz and 16 kHz, which is what
//  makes a tilted pink-noise curve read as a straight line.
//

#pragma once

#include "PageBase.h"

namespace GraphAxes
{
    //==============================================================================
    float frequencyToX (float hz, juce::Rectangle<float> plot, float minHz, float maxHz, bool logarithmic);
    float xToFrequency (float x,  juce::Rectangle<float> plot, float minHz, float maxHz, bool logarithmic);

    float decibelsToY (float db, juce::Rectangle<float> plot, float topDb, float bottomDb);
    float yToDecibels (float y,  juce::Rectangle<float> plot, float topDb, float bottomDb);

    //==============================================================================
    /** The sunk bed a graph is drawn on, plus its outline. */
    void paintBed (juce::Graphics&, const Theme&, juce::Rectangle<float> plot);

    /** Vertical lines at the decade and half-decade frequencies, with the
        labelled ones brighter. labelStrip may be empty to draw no labels. */
    void paintFrequencyGrid (juce::Graphics&, const Theme&,
                             juce::Rectangle<float> plot, juce::Rectangle<float> labelStrip,
                             float minHz, float maxHz, bool logarithmic);

    /** Horizontal lines every stepDb, labelled down the strip to the left. */
    void paintDecibelGrid (juce::Graphics&, const Theme&,
                           juce::Rectangle<float> plot, juce::Rectangle<float> labelStrip,
                           float topDb, float bottomDb, float stepDb);

    /** The scope's grid: a fixed number of divisions either way, with the zero
        line drawn brighter because it is the one that means something. */
    void paintDivisionGrid (juce::Graphics&, const Theme&,
                            juce::Rectangle<float> plot, int columns, int rows);

    /** "1 kHz", "440 Hz", "20 Hz" - the axis's own way of writing a frequency,
        used by the readouts too so that the number under the cursor is written
        the same way as the number on the axis. */
    juce::String formatFrequency (float hz);
}
