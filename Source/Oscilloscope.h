//
//  Oscilloscope.h
//  FX Analyzer
//
//  Pulls one screen's worth of waveform out of the capture ring, starting at a
//  trigger point rather than wherever the last frame happened to end.
//
//  Untriggered is offered and is not the default. On anything periodic a free
//  running trace slides sideways at the beat frequency between the display rate
//  and the signal, and the eye reads that motion as part of the sound. The
//  first thing anyone does on being shown a sliding scope is ask for the
//  trigger, so it is on from the start.
//
//  The trigger searches *backwards* from the newest sample for the most recent
//  crossing that still has a full window behind it. Searching forwards from the
//  oldest sample - the textbook way - finds the crossing furthest in the past,
//  which means the scope shows audio up to a second old and feels detached from
//  what is being heard.
//
//  The threshold is not zero. It is a small fraction of the recent peak,
//  because real material crosses zero several times per cycle on its way
//  through the noise floor and a zero-level trigger latches onto whichever of
//  those came last. Scaling the threshold to the signal is what keeps a quiet
//  passage triggering at all.
//

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

#include "ParameterIds.h"
#include "RingBuffer.h"

class Oscilloscope
{
public:
    Oscilloscope() = default;

    void prepare (double sampleRate, int maxTraceSamples);

    /** Reads a window of windowSeconds from the ring, trigger-aligned, into the
        traces. Channels 1 and 2 of the ring are taken to be left and right. */
    void capture (const CaptureRing& ring,
                  double sampleRate,
                  float windowSeconds,
                  FXParams::ScopeTrigger trigger);

    const std::vector<float>& getLeft()  const noexcept { return left; }
    const std::vector<float>& getRight() const noexcept { return right; }

    int getNumSamples() const noexcept { return numSamples; }

    /** Largest magnitude in the captured window. The page uses it for its
        auto-gain, and shows it as a number so the auto-gain is never a mystery
        multiplier. */
    float getPeakMagnitude() const noexcept { return peakMagnitude; }

    /** True when the window shown starts at a real trigger rather than at
        "wherever we were". The page dims its trigger label when it is false,
        because a scope that has quietly given up looks identical to one that
        has not. */
    bool isTriggered() const noexcept { return triggered; }

private:
    int findTrigger (const float* history, int historyLength, int windowSamples,
                     FXParams::ScopeTrigger trigger, float threshold) const;

    std::vector<float> left, right;
    std::vector<float> searchBuffer;

    int   numSamples    = 0;
    float peakMagnitude = 0.0f;
    bool  triggered     = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Oscilloscope)
};
