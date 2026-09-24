//
//  TruePeakMeter.h
//  FX Analyzer
//
//  True peak, the BS.1770 way: oversample 4x and take the largest magnitude
//  that appears between the samples you actually have.
//
//  Sample peak is not the same number and the difference is the whole point.
//  A limited master routinely reads -0.1 dBFS at the sample grid and +0.6 dBTP
//  once a converter or an MP3 encoder reconstructs the waveform between those
//  samples. If this plugin showed sample peak it would agree with the DAW's own
//  meter and mislead in exactly the situation the reader cares about.
//
//  The peak is held until reset rather than decaying. A mastering readout is
//  asked "did this ever go over?", and a number that fades away cannot answer
//  that. The reset button clears it alongside the integrated loudness, because
//  they are answers about the same stretch of programme.
//
//  One measured surprise is baked into this file. An oversampling FIR fed from
//  zeroed state overshoots on the first samples it sees, the same way any
//  filter rings at a step. AnalyzerCheck caught it: a full-scale inter-sample-peak
//  signal fed to a cold meter reads +0.81 dBTP, and the same signal fed to the
//  same meter once its filters have settled reads 0.00. That is not a rounding
//  error, it is most of a decibel of pure fiction, and it would have shown up
//  as material that "clips" the instant the plugin is inserted.
//
//  So the filters are primed: the first filter-length of samples after prepare()
//  advance the state without contributing a peak. At the start of playback there
//  is nothing there but silence anyway, and inserting an analyzer mid-passage
//  should not paint a red overload light that was never in the audio.
//

#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <memory>

class TruePeakMeter
{
public:
    TruePeakMeter() = default;

    /** Allocates the oversampler. Never call this from the audio thread. */
    void prepare (int numChannels, int maximumBlockSize);

    /** Clears the held peak, and nothing else. Deliberately does not touch the
        oversampler's filter state: this is the user's meter-reset button, it can
        be pressed in the middle of a loud passage, and re-cocking the filters
        there would make the next 2 ms read high. Real-time safe. */
    void reset() noexcept;

    /** Asks the audio thread to clear the held peak on its next block, so a
        press of the reset button cannot land between this thread's read of the
        maximum and its write back. */
    void requestReset() noexcept { resetRequested.store (true, std::memory_order_relaxed); }

    /** Real-time safe once prepared, provided numSamples <= the prepared
        maximum block size. Larger blocks are ignored rather than silently
        reallocating the oversampler underneath the audio thread. */
    void processBlock (const float* const* channelData, int numChannels, int numSamples) noexcept;

    /** Offline convenience: chunks the buffer to the prepared block size. */
    void processBuffer (const juce::AudioBuffer<float>& buffer) noexcept;

    /** Largest true peak since the last reset, in dBTP. -200 if nothing yet. */
    float getMaxTruePeakDb() const noexcept;

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    int preparedChannels = 0;
    int preparedBlockSize = 0;

    /** Input samples still to be pushed through before a peak counts. Set by
        prepare(), never by reset(). */
    int samplesToPrime = 0;

    std::atomic<bool>  resetRequested { false };
    std::atomic<float> maxMagnitude { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TruePeakMeter)
};
