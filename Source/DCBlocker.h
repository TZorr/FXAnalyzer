//
//  DCBlocker.h
//  FX Analyzer
//
//  A one-pole DC blocker, in front of the analysis and nowhere near the output.
//
//  It exists because of what DC does to the *display*, not to the audio. A few
//  thousandths of offset - the kind a modelled circuit or an asymmetric
//  waveshaper leaves behind and nobody hears - lands in the FFT's bin zero and
//  smears across the bottom two octaves through the analysis window's
//  sidelobes, which is exactly where somebody is trying to read whether the
//  kick has too much 40 Hz. On the scope it puts the trace off centre and
//  makes the trigger fire on the wrong edge.
//
//  It is switchable because a DC blocker is also a high-pass filter, and
//  somebody measuring what a plugin actually does to sub-bass is entitled to
//  see it. Corner at 5 Hz: below anything musical, above the offset.
//
//  Double state, single precision in and out. At 5 Hz and 96 kHz the pole sits
//  at 0.99967 and float state accumulates a slow drift over a few minutes -
//  the very thing the filter is here to remove.
//

#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

class DCBlocker
{
public:
    void prepare (double sampleRate) noexcept
    {
        // y[n] = x[n] - x[n-1] + R*y[n-1]. R placed from the corner frequency
        // rather than the usual hardcoded 0.995, which means a different corner
        // at every sample rate.
        constexpr double cornerHz = 5.0;
        pole = 1.0 - (2.0 * juce::MathConstants<double>::pi * cornerHz / juce::jmax (8000.0, sampleRate));
        reset();
    }

    void reset() noexcept { lastInput = 0.0; lastOutput = 0.0; }

    float process (float input) noexcept
    {
        const double x = (double) input;
        const double y = x - lastInput + pole * lastOutput;

        lastInput  = x;
        lastOutput = y;

        return (float) y;
    }

    void process (float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            samples[i] = process (samples[i]);
    }

private:
    double pole = 0.9993;
    double lastInput = 0.0, lastOutput = 0.0;
};
