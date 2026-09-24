//
//  LoudnessMeter.h
//  FX Analyzer
//
//  Loudness to ITU-R BS.1770-4: K-weighting, 400 ms momentary, 3 s short-term,
//  and a two-stage-gated integrated value.
//
//  The one thing worth reading this file for is that the filter coefficients
//  are *derived*, not copied. Every LUFS implementation on the internet quotes
//  the same table:
//
//      b = 1.53512485958697, -2.69169618940638, 1.19839281085285
//      a =                   -1.69065929318241, 0.73248077421585
//
//  Those numbers are the 48 kHz case and nothing else. Used at 44.1 kHz they
//  put the shelf at 1545 Hz instead of 1682 Hz; used at 96 kHz they are wrong
//  by an octave and the meter reads high on bright material. The spec gives
//  the filters as analogue prototypes precisely so they can be transformed for
//  the rate in hand, so that is what prepare() does - and AnalyzerCheck asserts
//  the 48 kHz derivation reproduces the published table to seven digits, which
//  is what makes the derivation trustworthy at every other rate.
//
//  The integrated value uses a fixed histogram rather than a growing list of
//  block loudnesses. The relative gate moves every time a block is added, so a
//  list would have to be rescanned anyway; a list would also have to grow, and
//  growing means allocating, and allocating is not allowed where this runs.
//  A thousand buckets scanned ten times a second is free.
//

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <array>

/** Reported when there is not yet enough material to state a value.
    -200 dB rather than -inf so it survives arithmetic and prints. */
inline constexpr float kLoudnessSilence = -200.0f;

class LoudnessMeter
{
public:
    LoudnessMeter() = default;

    /** Allocates. Message thread or loader thread only, never the audio thread. */
    void prepare (double sampleRate, int numChannels);

    /** Clears filter state, the sub-block ring and the integration histogram.
        Single-threaded use only - prepare(), or offline measurement. It walks a
        thousand-entry histogram and the sub-block ring, so calling it while the
        audio thread is inside processBlock is a genuine data race, not a
        theoretical one: the reading that comes out the other side can be
        anything, NaN included. */
    void reset() noexcept;

    /** Asks for a reset, to be performed by the audio thread itself at the top
        of its next block. This is what the meter-reset button calls. The wait
        is at most one buffer and costs the message thread nothing. */
    void requestReset() noexcept { resetRequested.store (true, std::memory_order_relaxed); }

    /** Real-time safe. Channels beyond the prepared count are ignored. */
    void processBlock (const float* const* channelData, int numChannels, int numSamples) noexcept;

    /** Convenience for offline measurement over a whole buffer. */
    void processBuffer (const juce::AudioBuffer<float>& buffer) noexcept;

    float getMomentaryLufs()  const noexcept { return momentary.load  (std::memory_order_relaxed); }
    float getShortTermLufs()  const noexcept { return shortTerm.load  (std::memory_order_relaxed); }
    float getIntegratedLufs() const noexcept { return integrated.load (std::memory_order_relaxed); }

    /** Loudness range to EBU Tech 3342, in LU: the 95th percentile of the
        gated short-term loudness minus the 10th. It answers a question the
        integrated value cannot - whether a master is uniformly loud or has
        somewhere to go - and it is the number that catches over-compression
        before anybody has to describe it in words.

        Percentiles, not maximum minus minimum. The two extremes of a piece of
        music are its loudest hit and its quietest gap, and their difference is
        a property of the fades, not of the material in between. */
    float getLoudnessRange() const noexcept { return range.load (std::memory_order_relaxed); }

    /** True once the integrated value rests on at least this many seconds of
        material above the absolute gate. The Loudness page greys the integrated
        readout until this is true, because an integrated number computed from
        half a bar reads like a measurement and behaves like a guess. */
    bool hasIntegratedFor (double seconds) const noexcept;

    //==============================================================================
    /** The BS.1770 K-weighting pair, exposed so AnalyzerCheck can assert the
        derivation against the published 48 kHz coefficients. */
    struct BiquadCoefficients
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    static BiquadCoefficients makeShelfCoefficients   (double sampleRate) noexcept;
    static BiquadCoefficients makeHighPassCoefficients (double sampleRate) noexcept;

private:
    //==============================================================================
    // Direct form II transposed, double state. Single precision is not enough
    // here: the RLB high-pass sits at 38 Hz, so at 96 kHz its poles are within
    // 0.002 of the unit circle and float state accumulates a visible DC drift
    // over a few minutes of material.
    struct Biquad
    {
        void setCoefficients (const BiquadCoefficients& c) noexcept { coeffs = c; }
        void reset() noexcept { s1 = s2 = 0.0; }

        double process (double x) noexcept
        {
            const double y = coeffs.b0 * x + s1;
            s1 = coeffs.b1 * x - coeffs.a1 * y + s2;
            s2 = coeffs.b2 * x - coeffs.a2 * y;
            return y;
        }

        BiquadCoefficients coeffs;
        double s1 = 0.0, s2 = 0.0;
    };

    struct ChannelFilter
    {
        void reset() noexcept { shelf.reset(); highPass.reset(); }
        double process (double x) noexcept { return highPass.process (shelf.process (x)); }

        Biquad shelf, highPass;
    };

    void closeSubBlock() noexcept;
    double windowPower (int numSubBlocks) const noexcept;
    void recomputeIntegrated() noexcept;
    void recomputeRange() noexcept;

    static float loudnessFromPower (double power) noexcept
    {
        return power > 0.0 ? (float) (-0.691 + 10.0 * std::log10 (power)) : kLoudnessSilence;
    }

    //==============================================================================
    static constexpr int maxChannels     = 2;   // this plugin is stereo throughout
    static constexpr int momentaryBlocks = 4;   // 4 x 100 ms = 400 ms
    static constexpr int shortTermBlocks = 30;  // 30 x 100 ms = 3 s
    static constexpr int ringSize        = 32;  // power of two, >= shortTermBlocks
    static constexpr int ringMask        = ringSize - 1;

    // Histogram of gating-block loudness: 0.1 dB buckets from -70 LUFS (the
    // absolute gate) upwards. Anything quieter than the first bucket is
    // discarded, which is the absolute gate implemented as a bounds check.
    static constexpr int    histogramSize   = 1000;
    static constexpr double histogramFloor  = -70.0;
    static constexpr double histogramStepDb = 0.1;

    std::array<ChannelFilter, maxChannels> filters;

    int preparedChannels = 2;
    int subBlockSamples  = 4800;   // 100 ms, replaced by prepare()

    double subBlockSum   = 0.0;
    int    subBlockCount = 0;

    std::array<double, ringSize> ringSum {};
    std::array<int,    ringSize> ringCount {};
    int   writeIndex   = 0;
    int64_t subBlocksSeen = 0;

    std::array<double,   histogramSize> histPower {};
    std::array<uint32_t, histogramSize> histCount {};
    int64_t gatedBlockCount = 0;

    // A second histogram, of short-term loudness rather than of 400 ms gating
    // blocks. It has to be separate: the range's relative gate sits 20 LU down
    // rather than 10, and it is applied to a distribution measured over a
    // different window. Reusing the integrated histogram and simply moving the
    // threshold would compute the range of the wrong quantity, and the answer
    // would be plausible enough that nobody would question it.
    std::array<double,   histogramSize> rangePower {};
    std::array<uint32_t, histogramSize> rangeCount {};
    int64_t rangeBlockCount = 0;

    std::atomic<bool> resetRequested { false };

    std::atomic<float> momentary  { kLoudnessSilence };
    std::atomic<float> shortTerm  { kLoudnessSilence };
    std::atomic<float> integrated { kLoudnessSilence };
    std::atomic<float> range      { kLoudnessSilence };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessMeter)
};
