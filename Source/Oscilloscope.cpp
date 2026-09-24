//
//  Oscilloscope.cpp
//  FX Analyzer
//

#include "Oscilloscope.h"

#include <cmath>

//==============================================================================
void Oscilloscope::prepare (double sampleRate, int maxTraceSamples)
{
    juce::ignoreUnused (sampleRate);

    left.assign  ((size_t) maxTraceSamples, 0.0f);
    right.assign ((size_t) maxTraceSamples, 0.0f);

    // The search window is the trace plus a trace's worth of history to look
    // back through. One extra window is enough to find a crossing in anything
    // whose fundamental fits on the screen at all, and asking for more would
    // start showing audio the listener has already forgotten.
    searchBuffer.assign ((size_t) maxTraceSamples * 2, 0.0f);

    numSamples    = 0;
    peakMagnitude = 0.0f;
    triggered     = false;
}

void Oscilloscope::capture (const CaptureRing& ring,
                            double sampleRate,
                            float windowSeconds,
                            FXParams::ScopeTrigger trigger)
{
    const auto maxTrace = (int) left.size();

    if (maxTrace == 0)
        return;

    numSamples = juce::jlimit (16, maxTrace, (int) std::round (windowSeconds * sampleRate));

    const auto searchLength = juce::jmin ((int) searchBuffer.size(), numSamples * 2);
    const auto endPosition  = ring.getWritePosition();

    // Channel 1 of the ring is the left input; the trigger runs on it because
    // triggering on the analysis channel would move the trace whenever the
    // Channel selector changed, which reads as the audio having changed.
    ring.read (1, endPosition, searchBuffer.data(), searchLength);

    peakMagnitude = 0.0f;

    for (int i = 0; i < searchLength; ++i)
        peakMagnitude = juce::jmax (peakMagnitude, std::abs (searchBuffer[(size_t) i]));

    // Ten percent of the recent peak. Low enough to catch a soft passage,
    // high enough to sit above the several crossings a complex waveform makes
    // per cycle on its way through zero.
    const auto threshold = juce::jmax (1.0e-5f, peakMagnitude * 0.1f);

    int startOffset = searchLength - numSamples;   // free-running fallback: the newest window
    triggered = false;

    if (trigger != FXParams::ScopeTrigger::free)
    {
        const auto found = findTrigger (searchBuffer.data(), searchLength, numSamples, trigger, threshold);

        if (found >= 0)
        {
            startOffset = found;
            triggered   = true;
        }
    }

    const auto traceEnd = endPosition - (int64_t) (searchLength - startOffset - numSamples);

    ring.read (1, traceEnd, left.data(),  numSamples);
    ring.read (2, traceEnd, right.data(), numSamples);
}

int Oscilloscope::findTrigger (const float* history, int historyLength, int windowSamples,
                               FXParams::ScopeTrigger trigger, float threshold) const
{
    const auto rising = trigger == FXParams::ScopeTrigger::rising;

    // Backwards from the newest sample that still has a full window behind it.
    for (int i = historyLength - windowSamples - 1; i > 0; --i)
    {
        const auto previous = history[i - 1];
        const auto current  = history[i];

        if (rising ? (previous < threshold && current >= threshold)
                   : (previous > -threshold && current <= -threshold))
            return i;
    }

    return -1;
}
