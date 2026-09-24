//
//  StereoAnalyser.h
//  FX Analyzer
//
//  Correlation, balance and width, accumulated on the audio thread because they
//  are the three numbers that must not miss a sample.
//
//  Correlation is the normalised cross-correlation of L and R over a sliding
//  window: +1 for a mono signal, 0 for two unrelated signals, -1 for a signal
//  against its own inversion. It is computed from running sums of LL, RR and LR
//  rather than from a buffer of samples, which is what lets the window be a
//  second long without a second's worth of memory.
//
//  Balance is compared as RMS, not as peak. A peak-based balance meter swings
//  on every snare hit and settles nowhere; the question being asked is "is the
//  mix sitting to the left", and that is a question about energy.
//
//  Width is the ratio of side energy to mid energy, reported in dB. Zero means
//  the sides carry as much as the middle - already very wide - and minus
//  infinity means mono. It is not a percentage, because every plugin that
//  reports width as a percentage means something different by it.
//
//  All three are smoothed with the same time constant the rest of the panel
//  uses, so that changing Reactivity changes the whole instrument rather than
//  five sixths of it.
//

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

class StereoAnalyser
{
public:
    StereoAnalyser() = default;

    void prepare (double sampleRate);

    void reset() noexcept;

    /** Real-time safe. Expects the two channels after the analysis chain has
        had its way with them; a mono input should pass the same pointer twice
        rather than a null, so that correlation reads +1 as it should. */
    void processBlock (const float* leftData, const float* rightData, int numSamples,
                       float smoothingSeconds) noexcept;

    /** -1 to +1. */
    float getCorrelation() const noexcept { return correlation.load (std::memory_order_relaxed); }

    /** -1 (hard left) to +1 (hard right), from RMS energy. */
    float getBalance() const noexcept { return balance.load (std::memory_order_relaxed); }

    /** Side energy over mid energy, in dB. kSilent when there is nothing to
        measure. */
    float getWidthDb() const noexcept { return widthDb.load (std::memory_order_relaxed); }

    float getRmsLeftDb()  const noexcept { return rmsLeftDb.load  (std::memory_order_relaxed); }
    float getRmsRightDb() const noexcept { return rmsRightDb.load (std::memory_order_relaxed); }

    static constexpr float kSilent = -200.0f;

private:
    double rate = 48000.0;

    // Running sums, decayed once per block rather than per sample. A per-sample
    // decay of a sum is three multiplies per sample for a number read thirty
    // times a second; per block it is three multiplies per block and the
    // difference is not measurable on the display.
    double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0;

    std::atomic<float> correlation { 0.0f };
    std::atomic<float> balance     { 0.0f };
    std::atomic<float> widthDb     { kSilent };
    std::atomic<float> rmsLeftDb   { kSilent };
    std::atomic<float> rmsRightDb  { kSilent };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoAnalyser)
};
